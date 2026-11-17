#include <Services/PlayerMarkerService.h>
#include <World.h>
#include <Components.h>
#include <Services/TransportService.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Forms/PlayerMarkerQuest.h>
#include <Forms/TESQuest.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <ScopedReference.h>
#include <algorithm>

PlayerMarkerService::PlayerMarkerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PlayerMarkerService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PlayerMarkerService::OnDisconnected>(this);
    m_actorRemovedConnection = aDispatcher.sink<ActorRemovedEvent>().connect<&PlayerMarkerService::OnActorRemoved>(this);
    m_playerRemovedConnection = m_world.on_destroy<PlayerComponent>().connect<&PlayerMarkerService::OnPlayerRemoved>(this);
    m_locationsConnection = aDispatcher.sink<NotifyPartyLocations>().connect<&PlayerMarkerService::OnLocations>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PlayerMarkerService::OnPartyLeft>(this);
}
void PlayerMarkerService::ClearSlot(uint32_t aSlot) noexcept
{
    if (auto* quest = PlayerMarkerQuest::Get())
        PlayerMarkerQuest::Clear(quest, aSlot);
    PlayerMarkerQuest::DestroyAnchor(m_slots[aSlot].AnchorHandle);
    m_slots[aSlot] = {};
}
bool PlayerMarkerService::ClearAll() noexcept
{
    auto* quest = PlayerMarkerQuest::Get();
    if (!quest || !PlayerMarkerQuest::Ready())
        return false;
    for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
    {
        // Also clears aliases restored from a save, which are absent from our
        // freshly constructed session slots. Never reuse an old dynamic actor.
        auto* old = quest->GetAliasedRef(slot);
        const uint32_t oldAnchor = PlayerMarkerQuest::IsAnchor(old) ? old->GetHandle().handle.iBits : 0;
        ScopedReference retained(oldAnchor);
        PlayerMarkerQuest::Clear(quest, slot);
        PlayerMarkerQuest::DestroyAnchor(m_slots[slot].AnchorHandle);
        if (oldAnchor != m_slots[slot].AnchorHandle)
            PlayerMarkerQuest::DestroyAnchor(oldAnchor);
        m_slots[slot] = {};
    }
    quest->SetActive(false);
    return true;
}
void PlayerMarkerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    ClearAll();
    m_needsCleanup = true;
    m_nextUpdate = 0;
    m_locations.Players.clear();
    m_lastSequence = m_receivedAt = 0;
}
void PlayerMarkerService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
        if (m_slots[slot].FormId == acEvent.FormId)
        {
            if (auto* quest = PlayerMarkerQuest::Get())
                PlayerMarkerQuest::Clear(quest, slot);
            m_slots[slot].FormId = 0; // The next tick may use a distant anchor.
        }
}
void PlayerMarkerService::OnPlayerRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const uint32_t player = aRegistry.get<PlayerComponent>(aEntity).Id;
    for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
        if (m_slots[slot].Player == player)
        {
            if (auto* quest = PlayerMarkerQuest::Get())
                PlayerMarkerQuest::Clear(quest, slot);
            m_slots[slot].FormId = 0;
        }
}
void PlayerMarkerService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    ClearAll();
    m_locations.Players.clear();
    m_receivedAt = 0;
}
void PlayerMarkerService::OnLocations(const NotifyPartyLocations& acMessage) noexcept
{
    const auto& party = m_world.GetPartyService();
    if (!m_transport.IsConnected() || !party.IsInParty() || !acMessage.IsValid() || acMessage.Sequence <= m_lastSequence ||
        acMessage.LeaderPlayerId != party.GetLeaderPlayerId())
        return;
    const auto& members = party.GetPartyMembers();
    for (const auto& location : acMessage.Players)
        if (std::find(members.begin(), members.end(), location.PlayerId) == members.end())
            return;
    const uint64_t now = m_world.GetTick();
    if ((acMessage.Tick > now && acMessage.Tick - now > 2000) || (now > acMessage.Tick && now - acMessage.Tick > NotifyPartyLocations::kMaxAgeMs))
        return;
    m_lastSequence = acMessage.Sequence;
    m_receivedAt = GetTickCount64();
    m_locations = acMessage;
}
void PlayerMarkerService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_world.GetOverlayService().GetInGame() || !PlayerMarkerQuest::Ready())
        return;
    const uint64_t now = GetTickCount64();
    if (now < m_nextUpdate)
        return;
    m_nextUpdate = now + 250;
    if (m_needsCleanup)
    {
        if (!ClearAll())
            return;
        m_needsCleanup = false;
    }
    auto* quest = PlayerMarkerQuest::Get();
    if (!quest)
        return;
    if (!m_transport.IsConnected() || !m_world.GetPartyService().IsInParty())
    {
        for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
            if (m_slots[slot].Player)
                ClearSlot(slot);
        return;
    }
    const auto& members = m_world.GetPartyService().GetPartyMembers();
    const auto fresh = [&](const NotifyPartyLocations::Location& location)
    {
        const uint64_t elapsed = now - m_receivedAt;
        return m_receivedAt != 0 && elapsed <= NotifyPartyLocations::kMaxAgeMs && location.ReceivedTick <= m_locations.Tick &&
               m_locations.Tick - location.ReceivedTick <= NotifyPartyLocations::kMaxAgeMs - elapsed &&
               location.PlayerId != m_transport.GetLocalPlayerId() && std::find(members.begin(), members.end(), location.PlayerId) != members.end();
    };
    for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
    {
        if (!m_slots[slot].Player)
            continue;
        const bool keep = std::any_of(m_locations.Players.begin(), m_locations.Players.end(), [&](const auto& location)
        {
            return location.PlayerId == *m_slots[slot].Player && fresh(location);
        });
        if (!keep)
            ClearSlot(slot);
    }
    bool createdAnchor{};
    const auto view = m_world.view<PlayerComponent, FormIdComponent, RemoteComponent>(entt::exclude<WaitingFor3D>);
    for (const auto& location : m_locations.Players)
    {
        if (!fresh(location))
            continue;
        uint32_t index = PlayerMarkers::kSlotCount;
        for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
            if (m_slots[slot].Player == location.PlayerId)
            {
                index = slot;
                break;
            }
        if (index == PlayerMarkers::kSlotCount)
            for (uint32_t slot = 0; slot < m_slots.size(); ++slot)
                if (!m_slots[slot].Player)
                {
                    index = slot;
                    m_slots[slot].Player = location.PlayerId;
                    break;
                }
        if (index == PlayerMarkers::kSlotCount)
            continue;
        auto& slot = m_slots[index];
        Actor* actor{};
        for (const auto entity : view)
            if (view.get<PlayerComponent>(entity).Id == location.PlayerId &&
                view.get<RemoteComponent>(entity).Id == location.ServerId &&
                view.get<RemoteComponent>(entity).OwnershipEpoch == location.OwnershipEpoch)
            {
                actor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
                break;
            }
        if (actor && !actor->IsDeleted() && !actor->IsDisabled() && actor->GetNiNode() && actor->GetExtension()->IsRemotePlayer())
        {
            if (slot.FormId != actor->formID || quest->GetAliasedRef(index) != actor)
                if (PlayerMarkerQuest::Bind(quest, index, actor))
                {
                    slot.FormId = actor->formID;
                    PlayerMarkerQuest::DestroyAnchor(slot.AnchorHandle);
                    slot.AnchorHandle = 0;
                }
            continue;
        }
        if (slot.AnchorHandle && (slot.Cell != location.Cell || slot.WorldSpace != location.WorldSpace))
        {
            PlayerMarkerQuest::Clear(quest, index);
            PlayerMarkerQuest::DestroyAnchor(slot.AnchorHandle);
            slot.FormId = slot.AnchorHandle = 0;
        }
        NiPoint3 position{};
        position.x = location.Position.x;
        position.y = location.Position.y;
        position.z = location.Position.z;
        if (!slot.AnchorHandle)
        {
            // At most one native reference allocation per light service tick.
            if (createdAnchor)
                continue;
            createdAnchor = true;
            slot.AnchorHandle = PlayerMarkerQuest::CreateAnchor(location.Cell, location.WorldSpace, position);
            slot.Cell = location.Cell;
            slot.WorldSpace = location.WorldSpace;
        }
        if (!PlayerMarkerQuest::MoveAnchor(slot.AnchorHandle, position))
        {
            PlayerMarkerQuest::Clear(quest, index);
            PlayerMarkerQuest::DestroyAnchor(slot.AnchorHandle);
            slot.AnchorHandle = slot.FormId = 0;
            continue;
        }
        ScopedReference reference(slot.AnchorHandle);
        if (reference && (slot.FormId != reference.Get()->formID || quest->GetAliasedRef(index) != reference.Get()))
            if (PlayerMarkerQuest::Bind(quest, index, reference.Get()))
                slot.FormId = reference.Get()->formID;
    }
}
