#include <World.h>
#include <GameServer.h>
#include <Components.h>
#include <Services/WorldItemService.h>
#include <Services/WorldCheckpointService.h>
#include <algorithm>

WorldItemService::WorldItemService(World& w, entt::dispatcher& d) noexcept
    : m_world(w)
{
    m_queryConnection = d.sink<PacketEvent<RequestWorldItems>>().connect<&WorldItemService::OnQuery>(this);
    m_transferConnection = d.sink<PacketEvent<RequestWorldItemTransfer>>().connect<&WorldItemService::OnTransfer>(this);
    m_motionConnection = d.sink<PacketEvent<RequestWorldItemMotion>>().connect<&WorldItemService::OnMotion>(this);
}
WorldItemService::Client& WorldItemService::GetClient(Player& p) noexcept
{
    std::erase_if(
        m_clients,
        [&](const auto& c)
        {
            const auto* live = m_world.GetPlayerManager().GetById(c.Id);
            return !live || live->GetCampaignNonce() != c.Admission;
        });
    for (auto& c : m_clients)
        if (c.Id == p.GetId())
            return c;
    Client c;
    c.Id = p.GetId();
    c.Admission = p.GetCampaignNonce();
    m_clients.push_back(std::move(c));
    return m_clients.back();
}
bool WorldItemService::Ready(Player& p, uint64_t& hostEpoch) noexcept
{
    if (!p.IsCampaignReady() || !p.GetCharacter() || m_world.GetPlayerManager().Count() != 2 || m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return false;
    const auto* party = m_world.GetPartyService().GetPlayerParty(&p);
    auto* host = party ? m_world.GetPlayerManager().GetById(party->LeaderPlayerId) : nullptr;
    if (!party || party->Members.size() != 2 || !host || !host->IsCampaignReady())
        return false;
    for (const auto* member : party->Members)
        if (!member->IsCampaignReady() || !member->GetCharacter())
            return false;
    const uint64_t admission = host->GetCampaignNonce();
    if (!admission || !Load(admission))
        return false;
    hostEpoch = m_hostEpoch;
    return true;
}
bool WorldItemService::Load(uint64_t hostEpoch) noexcept
{
    const uint64_t epoch = m_world.GetCampaign().WorldEpoch();
    if (epoch == m_epoch && hostEpoch == m_leaderAdmission)
        return true;
    if (epoch != m_epoch)
    {
        Vector<WorldItemState> stored;
        if (!m_world.GetCampaign().LoadWorldItems(stored))
        {
            GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
            return false;
        }
        m_items.clear();
        m_clients.clear();
        for (auto& state : stored)
            if (!state.Deleted)
            {
                m_hostEpoch = std::max(m_hostEpoch, state.HostEpoch);
                Item item;
                item.State = std::move(state);
                m_items.push_back(std::move(item));
            }
        if (m_items.size() > WorldItemWire::kSlots)
        {
            GameServer::Get()->StopForWorldError("Saved world exceeds the native world-item alias capacity");
            return false;
        }
    }
    if (m_hostEpoch == UINT64_MAX)
    {
        GameServer::Get()->StopForWorldError("World-item ownership epoch exhausted");
        return false;
    }
    m_epoch = epoch;
    m_leaderAdmission = hostEpoch;
    ++m_hostEpoch;
    for (auto& item : m_items)
    {
        item.State.WorldEpoch = epoch;
        item.State.HostEpoch = m_hostEpoch;
        item.State.Sequence = 1;
        item.State.Tick = GameServer::Get()->GetTick();
        item.Received = 0;
        item.Interactor = 0;
        item.InputSequence = 0;
        item.Dirty = true;
    }
    return true;
}
void WorldItemService::Snapshot(NotifyWorldItems& reply) noexcept
{
    reply.HostEpoch = m_hostEpoch;
    reply.Head = m_world.GetCampaign().Revision();
    for (const auto& item : m_items)
        reply.Items.push_back(item.State);
}
void WorldItemService::Broadcast() noexcept
{
    NotifyWorldItems reply;
    Snapshot(reply);
    for (auto* p : m_world.GetPlayerManager())
        if (p->IsCampaignReady())
            p->Send(reply);
}
void WorldItemService::OnQuery(const PacketEvent<RequestWorldItems>& packet) noexcept
{
    auto& p = *packet.pPlayer;
    const auto& req = packet.Packet;
    uint64_t hostEpoch{};
    if (!req.IsValid() || !Ready(p, hostEpoch))
        return;
    auto& client = GetClient(p);
    const uint64_t now = GameServer::Get()->GetTick();
    if (now < client.NextQuery)
        return;
    client.NextQuery = now + 200;
    if (req.After > m_world.GetCampaign().Revision())
    {
        GameServer::Get()->RejectCampaign(p.GetConnectionId(), "Item cursor is newer than this world; restore the paired checkpoint");
        return;
    }
    Vector<Campaign::WorldItemReceipt> receipts;
    if (!m_world.GetCampaign().ReadWorldItemReceipts(p.GetPersistentCharacterId(), req.After, receipts))
    {
        GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
        return;
    }
    if (client.Awaiting && req.After >= client.Awaiting)
        client.Awaiting = 0;
    NotifyWorldItems reply;
    reply.Nonce = req.Nonce;
    reply.After = req.After;
    Snapshot(reply);
    if (!receipts.empty() && !Campaign::Encode(receipts.front(), reply.Receipt))
    {
        GameServer::Get()->StopForWorldError("Cannot encode world-item receipt");
        return;
    }
    p.Send(reply);
}
void WorldItemService::OnTransfer(const PacketEvent<RequestWorldItemTransfer>& packet) noexcept
{
    auto& p = *packet.pPlayer;
    const auto& req = packet.Packet;
    uint64_t hostEpoch{};
    if (!req.IsValid() || req.Admission != p.GetCampaignNonce() || !Ready(p, hostEpoch))
        return;
    auto& client = GetClient(p);
    if (req.Operation == client.Last.Operation && req.Session == client.Last.Session)
    {
        if (req.Delta == client.Last.Delta && req.State == client.Last.State)
            p.Send(client.Reply);
        return;
    }
    if (client.Awaiting || (client.Last.Operation && (req.Session != client.Last.Session || req.Operation <= client.Last.Operation)))
        return;
    NotifyWorldItemTransfer reply;
    reply.Operation = req.Operation;
    reply.Admission = req.Admission;
    auto remember = [&]()
    {
        client.Last = req;
        client.Reply = reply;
        p.Send(reply);
    };
    const auto* cell = m_world.try_get<CellIdComponent>(*p.GetCharacter());
    const auto* move = m_world.try_get<MovementComponent>(*p.GetCharacter());
    if (!cell || !move || req.State.WorldEpoch != m_epoch || req.State.HostEpoch != hostEpoch || req.State.CellId != cell->Cell || req.State.WorldSpaceId != cell->WorldSpaceId)
    {
        remember();
        return;
    }
    WorldItemState state = req.State;
    uint64_t expected{};
    auto found = std::find_if(m_items.begin(), m_items.end(), [&](const auto& i) { return i.State.Id == state.Id; });
    if (req.Delta < 0)
    {
        if (m_items.size() >= WorldItemWire::kSlots || state.Item.Count != -req.Delta)
        {
            remember();
            return;
        }
        state.Id = state.Revision = 0;
    }
    else
    {
        if (found == m_items.end() || state.Revision != found->State.Revision || req.Delta > found->State.Item.Count)
        {
            remember();
            return;
        }
        state = found->State;
        expected = state.Revision;
    }
    double distance{};
    for (int i = 0; i < 3; ++i)
    {
        const double delta = double(state.Position[i]) - move->Position[i];
        distance += delta * delta;
    }
    if (distance > 400. * 400.)
    {
        remember();
        return;
    }
    state.Tick = GameServer::Get()->GetTick();
    state.Sequence = std::max(uint64_t(1), state.Sequence);
    Campaign::WorldItemReceipt receipt;
    const auto result = m_world.GetCampaign().TransferWorldItem(state, expected, p.GetPersistentCharacterId(), req.Session, req.Operation, req.Delta, receipt);
    if (result == Campaign::State::ItemTransferResult::StorageError)
    {
        GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
        return;
    }
    if (result == Campaign::State::ItemTransferResult::Committed || result == Campaign::State::ItemTransferResult::Duplicate)
    {
        reply.Accepted = true;
        reply.Revision = receipt.State.Revision;
        client.Awaiting = reply.Revision;
        if (req.Delta < 0)
        {
            Item item;
            item.State = state;
            m_items.push_back(std::move(item));
        }
        else if (state.Deleted)
            m_items.erase(found);
        else
        {
            found->State = state;
            found->Dirty = false;
        }
    }
    remember();
    if (reply.Accepted)
        Broadcast();
}
void WorldItemService::OnMotion(const PacketEvent<RequestWorldItemMotion>& packet) noexcept
{
    auto& p = *packet.pPlayer;
    const auto& req = packet.Packet;
    const auto& s = req.State;
    uint64_t hostEpoch{};
    if (!req.Decoded || !s.IsValid() || !Ready(p, hostEpoch) || s.WorldEpoch != m_epoch || s.HostEpoch != hostEpoch)
        return;
    auto found = std::find_if(m_items.begin(), m_items.end(), [&](const auto& i) { return i.State.Id == s.Id; });
    if (found == m_items.end() || found->State.Revision != s.Revision || found->State.CellId != s.CellId || found->State.WorldSpaceId != s.WorldSpaceId)
        return;
    auto& item = *found;
    const uint64_t now = GameServer::Get()->GetTick();
    if (s.Tick > now ? s.Tick - now > 1000 : now - s.Tick > 1000)
        return;
    const auto* party = m_world.GetPartyService().GetPlayerParty(&p);
    auto* host = party ? m_world.GetPlayerManager().GetById(party->LeaderPlayerId) : nullptr;
    if (!host || p.GetCellComponent().Cell != s.CellId || p.GetCellComponent().WorldSpaceId != s.WorldSpaceId)
        return;
    if (req.Interaction)
    {
        const auto* move = m_world.try_get<MovementComponent>(*p.GetCharacter());
        if (&p == host || !move || !item.Received || now > item.Received + 500 || (item.Interactor && item.Interactor != p.GetId() && now < item.Interacted + 500) ||
            (item.Interactor == p.GetId() && (s.Sequence <= item.InputSequence || now < item.Interacted + 40)))
            return;
        double distance{}, displacement{}, speed{}, spin{};
        for (int i = 0; i < 3; ++i)
        {
            const double d = double(item.State.Position[i]) - move->Position[i], x = double(s.Position[i]) - item.State.Position[i];
            distance += d * d;
            displacement += x * x;
            speed += double(s.LinearVelocity[i]) * s.LinearVelocity[i];
            spin += double(s.AngularVelocity[i]) * s.AngularVelocity[i];
        }
        if (distance > 400. * 400. || displacement > 256. * 256. || speed > 4096. * 4096. || spin > 32. * 32.)
            return;
        item.Interactor = p.GetId();
        item.Interacted = now;
        item.InputSequence = s.Sequence;
        NotifyWorldItemMotion reply;
        reply.State = s;
        reply.State.Tick = now;
        reply.Interaction = true;
        host->Send(reply, TiltedPhoques::kUnreliableNoNagle);
        return;
    }
    if (&p != host || s.Sequence <= item.State.Sequence || (item.Received && now < item.Received + 40))
        return;
    static_cast<WorldItemMotion&>(item.State) = s;
    item.State.Tick = now;
    item.Received = now;
    item.Dirty = true;
    NotifyWorldItemMotion reply;
    reply.State = item.State;
    for (auto* member : party->Members)
        if (member != host)
            member->Send(reply, TiltedPhoques::kUnreliableNoNagle);
}
bool WorldItemService::HasPending() const noexcept
{
    return std::any_of(
        m_clients.begin(), m_clients.end(),
        [&](const auto& c)
        {
            const auto* p = m_world.GetPlayerManager().GetById(c.Id);
            return c.Awaiting && p && p->IsCampaignReady() && p->GetCampaignNonce() == c.Admission;
        });
}
bool WorldItemService::Flush() noexcept
{
    if (HasPending())
        return false;
    for (auto& item : m_items)
        if (item.Dirty)
        {
            if (item.State.Sequence == UINT64_MAX)
            {
                GameServer::Get()->StopForWorldError("World-item motion sequence exhausted");
                return false;
            }
            auto state = item.State;
            ++state.Sequence;
            if (m_world.GetCampaign().SaveWorldItem(state, state.Revision) != Campaign::State::ItemWriteResult::Saved)
            {
                GameServer::Get()->StopForWorldError(m_world.GetCampaign().Error());
                return false;
            }
            item.State = state;
            item.Dirty = false;
        }
    if (!m_items.empty())
        Broadcast();
    return true;
}
