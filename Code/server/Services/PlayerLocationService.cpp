#include "PlayerLocationService.h"
#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPartyLocations.h>
#include <limits>

PlayerLocationService::PlayerLocationService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PlayerLocationService::OnUpdate>(this);
}
void PlayerLocationService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t now = GameServer::Get()->GetTick();
    if (now < m_nextUpdate || m_sequence == std::numeric_limits<uint64_t>::max())
        return;
    m_nextUpdate = now + 500;
    ++m_sequence;
    Vector<uint32_t> visited;
    for (Player* recipient : m_world.GetPlayerManager())
    {
        const auto partyId = recipient->GetParty().JoinedPartyId;
        if (!recipient->IsCampaignReady() || !partyId || std::find(visited.begin(), visited.end(), *partyId) != visited.end())
            continue;
        visited.push_back(*partyId);
        const auto* party = m_world.GetPartyService().GetById(*partyId);
        if (!party || party->Members.size() > NotifyPartyLocations::kMaxPlayers)
            continue;
        NotifyPartyLocations snapshot;
        snapshot.LeaderPlayerId = party->LeaderPlayerId;
        snapshot.Sequence = m_sequence;
        snapshot.Tick = now;
        for (const Player* player : party->Members)
        {
            const auto character = player->GetCharacter();
            if (!player->IsCampaignReady() || !character)
                continue;
            const auto* owner = m_world.try_get<OwnerComponent>(*character);
            const auto* movement = m_world.try_get<MovementComponent>(*character);
            const auto* cell = m_world.try_get<CellIdComponent>(*character);
            if (!owner || owner->GetOwner() != player || !movement || !cell || cell->Cell != player->GetCellComponent().Cell ||
                cell->WorldSpaceId != player->GetCellComponent().WorldSpaceId || movement->ReceivedCell != cell->Cell ||
                movement->ReceivedWorldSpace != cell->WorldSpaceId)
                continue;
            NotifyPartyLocations::Location location;
            location.PlayerId = player->GetId();
            location.ServerId = static_cast<uint32_t>(*character);
            location.OwnershipEpoch = owner->OwnershipEpoch;
            location.ReceivedTick = movement->ReceivedTick;
            location.Cell = cell->Cell;
            location.WorldSpace = cell->WorldSpaceId;
            location.Position = movement->Position;
            if (location.IsValid(now))
                snapshot.Players.push_back(location);
        }
        // Deliberately party-wide: the peer may be in another grid or interior.
        // A full bounded snapshot also removes stale/departed map targets.
        for (const Player* player : party->Members)
            if (player->IsCampaignReady())
                player->Send(snapshot);
    }
}
