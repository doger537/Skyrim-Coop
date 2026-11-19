#include <Services/PhysicsCheckpointService.h>
#include <Services/PlacedPhysicsService.h>
#include <Services/WorldItemService.h>
#include <World.h>
#include <Components.h>
#include <common/PhysicsFrameHash.h>
#include <algorithm>

PhysicsCheckpointService::PhysicsCheckpointService(World& w, entt::dispatcher& d) noexcept
    : m_world(w)
{
    m_packet = d.sink<PacketEvent<RequestPhysicsFrame>>().connect<&PhysicsCheckpointService::OnPacket>(this);
    m_update = d.sink<UpdateEvent>().connect<&PhysicsCheckpointService::OnUpdate>(this);
}
void PhysicsCheckpointService::Cancel() noexcept
{
    m_active = m_canonical = m_failed = false;
    m_applied = 0;
    m_control = {};
    m_offers = {};
    m_frame = {};
    m_sent = {};
    m_ended = {};
    m_digest = {};
}
bool PhysicsCheckpointService::Capture(const NotifyWorldCheckpoint& control) noexcept
{
    Cancel();
    m_control = control;
    m_frame.Checkpoint = control.Id;
    for (auto& offer : m_offers)
        offer.Checkpoint = control.Id;
    m_active = true;
    return true;
}
bool PhysicsCheckpointService::Load(const NotifyWorldCheckpoint& control) noexcept
{
    Cancel();
    m_control = control;
    m_digest = control.Manifest.PhysicsDigest;
    if (!m_world.GetCampaign().ReadPhysicsFrame(control.Id, m_digest, m_frame))
        return false;
    if (m_frame.Entries.size() != control.Manifest.PhysicsEntries)
        return false;
    m_active = m_canonical = true;
    return true;
}
bool PhysicsCheckpointService::Merge() noexcept
{
    if (m_offers[0].NativeToWorld != m_offers[1].NativeToWorld)
        return false;
    m_frame.NativeToWorld = m_offers[0].NativeToWorld;
    std::array<size_t, 2> cursor{};
    while (cursor[0] < m_offers[0].Entries.size() || cursor[1] < m_offers[1].Entries.size())
    {
        const PhysicsFrameEntry* values[2]{};
        for (size_t i = 0; i < 2; ++i)
            if (cursor[i] < m_offers[i].Entries.size())
                values[i] = &m_offers[i].Entries[cursor[i]];
        if (values[0] && values[1] && !values[0]->SameKey(*values[1]))
        {
            if (PhysicsFrameEntry::Less(*values[0], *values[1]))
                values[1] = nullptr;
            else
                values[0] = nullptr;
        }
        const auto& key = *(values[0] ? values[0] : values[1]);
        size_t source = values[0] ? 0 : 1;
        uint32_t authority{};
        if (key.Type == PhysicsFrameEntry::Actor)
        {
            if (!m_world.valid(static_cast<entt::entity>(key.Id)))
                return false;
            const auto* owner = m_world.try_get<OwnerComponent>(static_cast<entt::entity>(key.Id));
            if (!owner || !owner->GetOwner() || owner->OwnershipEpoch != key.Epoch)
                return false;
            const auto character = owner->GetOwner()->GetPersistentCharacterId();
            source = character == m_control.Members[0] ? 0 : character == m_control.Members[1] ? 1 : 2;
            if (source > 1 || !values[source])
                return false;
        }
        else if (key.Type == PhysicsFrameEntry::Placed)
        {
            if (!m_world.GetCampaign().ValidForm(key.Reference))
                return false;
            if (const auto* state = m_world.ctx().at<PlacedPhysicsService>().Find(key.Reference))
            {
                if (state->Retired)
                    return false;
                authority = state->Owner;
            }
        }
        else
        {
            const auto* state = m_world.ctx().at<WorldItemService>().Find(key.Id);
            if (!state || state->Deleted || state->HostEpoch != key.Epoch || key.Rig.Count != 1)
                return false;
        }
        if (authority)
            for (size_t i = 0; i < 2; ++i)
                for (auto* player : m_world.GetPlayerManager())
                    if (player->GetId() == authority && player->GetPersistentCharacterId() == m_control.Members[i] && values[i])
                        source = i;
        PhysicsFrameEntry canonical = *values[source];
        canonical.SourceSlot = static_cast<uint8_t>(source);
        canonical.PresentMask = 0;
        for (size_t i = 0; i < 2; ++i)
            if (values[i])
            {
                if (!m_world.GetCampaign().ValidForm(values[i]->Cell) || !m_world.GetCampaign().ValidForm(values[i]->WorldSpace, true) ||
                    values[i]->WorldSpace != canonical.WorldSpace || (!canonical.WorldSpace && values[i]->Cell != canonical.Cell) || !values[i]->Rig.SameGraph(canonical.Rig) ||
                    (key.Type != PhysicsFrameEntry::Placed && values[i]->Epoch != canonical.Epoch))
                    return false;
                canonical.PresentMask |= static_cast<uint8_t>(1 << i);
                ++cursor[i];
            }
        if (m_frame.Entries.size() >= PhysicsFrame::kMaxEntries)
            return false;
        m_frame.Entries.push_back(std::move(canonical));
    }
    Vector<uint8_t> bytes;
    if (!m_frame.Encode(bytes))
        return false;
    m_digest = HashPhysicsFrame(bytes);
    m_canonical = true;
    return true;
}
void PhysicsCheckpointService::OnPacket(const PacketEvent<RequestPhysicsFrame>& event) noexcept
{
    const auto& request = event.Packet;
    const auto& data = request.Data;
    if (!m_active || m_failed || !request.IsValid() || data.Id != m_control.Id || data.Nonce != m_control.Nonce || !event.pPlayer->IsCampaignReady())
        return;
    const auto character = event.pPlayer->GetPersistentCharacterId();
    const size_t slot = character == m_control.Members[0] ? 0 : character == m_control.Members[1] ? 1 : 2;
    if (slot > 1)
        return;
    if (request.Action == RequestPhysicsFrame::Failed)
    {
        m_failed = true;
        return;
    }
    if (request.Action == RequestPhysicsFrame::Applied)
    {
        if (!m_canonical || !m_ended[slot] || data.Digest != m_digest || data.Index != m_frame.Entries.size() || data.NativeToWorld != m_frame.NativeToWorld)
        {
            m_failed = true;
            return;
        }
        m_applied |= static_cast<uint8_t>(1 << slot);
        return;
    }
    if (m_canonical || m_ended[slot])
    {
        m_failed = true;
        return;
    }
    auto& offer = m_offers[slot];
    if (data.Index != offer.Entries.size() || (!offer.Entries.empty() && data.NativeToWorld != offer.NativeToWorld))
    {
        m_failed = true;
        return;
    }
    offer.NativeToWorld = data.NativeToWorld;
    if (request.Action == RequestPhysicsFrame::Offer)
    {
        for (const auto& entry : data.Entries)
        {
            if (entry.SourceSlot != slot || entry.PresentMask != (1 << slot) || (!offer.Entries.empty() && !PhysicsFrameEntry::Less(offer.Entries.back(), entry)))
            {
                m_failed = true;
                return;
            }
            offer.Entries.push_back(entry);
        }
    }
    else
    {
        if (!offer.IsValid())
        {
            m_failed = true;
            return;
        }
        m_ended[slot] = true;
        if (m_ended[0] && m_ended[1])
        {
            m_ended = {};
            if (!Merge())
                m_failed = true;
        }
    }
}
void PhysicsCheckpointService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_active || !m_canonical || m_failed)
        return;
    for (auto* player : m_world.GetPlayerManager())
    {
        const auto character = player->GetPersistentCharacterId();
        const size_t slot = character == m_control.Members[0] ? 0 : character == m_control.Members[1] ? 1 : 2;
        if (slot > 1 || !player->IsCampaignReady() || m_ended[slot])
            continue;
        NotifyPhysicsFrame packet;
        auto& data = packet.Data;
        data.Id = m_control.Id;
        data.Nonce = m_control.Nonce;
        data.NativeToWorld = m_frame.NativeToWorld;
        data.Index = static_cast<uint32_t>(m_sent[slot]);
        data.Digest = m_digest;
        if (m_sent[slot] < m_frame.Entries.size())
        {
            packet.Action = NotifyPhysicsFrame::Canonical;
            const size_t end = std::min(m_frame.Entries.size(), m_sent[slot] + 8);
            for (; m_sent[slot] < end; ++m_sent[slot])
                data.Entries.push_back(m_frame.Entries[m_sent[slot]]);
        }
        else
        {
            packet.Action = NotifyPhysicsFrame::End;
            m_ended[slot] = true;
        }
        player->Send(packet);
    }
}
