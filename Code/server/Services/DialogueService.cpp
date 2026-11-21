#include "DialogueService.h"
#include <Components.h>
#include <World.h>
#include <GameServer.h>
#include <Events/UpdateEvent.h>
#include <Messages/RequestDialogueLease.h>
#include <Messages/NotifyDialogueLease.h>
#include <limits>
#include <cmath>

DialogueService::DialogueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_requestConnection = aDispatcher.sink<PacketEvent<RequestDialogueLease>>().connect<&DialogueService::OnRequest>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DialogueService::OnUpdate>(this);
}

Player* DialogueService::FindPlayer(uint32_t aId) const noexcept
{
    for (Player* player : m_world.GetPlayerManager())
        if (player->GetId() == aId)
            return player;
    return nullptr;
}

bool DialogueService::CanTalk(Player* apSpeaker, entt::entity aActor) const noexcept
{
    if (!apSpeaker || !apSpeaker->IsCampaignReady() || !apSpeaker->GetParty().JoinedPartyId || !apSpeaker->GetCharacter())
        return false;
    const auto* party = m_world.GetPartyService().GetById(*apSpeaker->GetParty().JoinedPartyId);
    const auto* character = m_world.try_get<CharacterComponent>(aActor);
    const auto* cell = m_world.try_get<CellIdComponent>(aActor);
    const auto* owner = m_world.try_get<OwnerComponent>(aActor);
    const auto* movement = m_world.try_get<MovementComponent>(aActor);
    const auto* speakerMovement = m_world.try_get<MovementComponent>(*apSpeaker->GetCharacter());
    if (!party || party->Members.size() != 2 || !character || character->IsPlayer() || character->IsDead() || !cell ||
        !owner || !owner->GetOwner() || !movement || !speakerMovement ||
        !apSpeaker->GetCellComponent().IsInRange(*cell, character->IsDragon()))
        return false;
    const auto* playerOwner = owner->GetOwner();
    if (!playerOwner->IsCampaignReady() || playerOwner->GetParty().JoinedPartyId != apSpeaker->GetParty().JoinedPartyId)
        return false;
    const glm::vec3 delta = movement->Position - speakerMovement->Position;
    const float distanceSquared = glm::dot(delta, delta);
    return std::isfinite(distanceSquared) && distanceSquared <= 1024.f * 1024.f;
}

void DialogueService::Broadcast(entt::entity aActor, const DialogueLeaseComponent& acLease, bool aActive) noexcept
{
    if (m_revision == std::numeric_limits<uint64_t>::max())
        return;
    NotifyDialogueLease notify;
    notify.ServerId = static_cast<uint32_t>(aActor);
    notify.OwnershipEpoch = acLease.OwnershipEpoch;
    notify.SpeakerPlayerId = acLease.SpeakerPlayerId;
    notify.Revision = ++m_revision;
    notify.Active = aActive;
    notify.Deadline = aActive ? acLease.Deadline : 0;
    if (Player* owner = FindPlayer(acLease.OwnerPlayerId); owner && owner->IsCampaignReady())
        owner->Send(notify);
    if (acLease.SpeakerPlayerId != acLease.OwnerPlayerId)
        if (Player* speaker = FindPlayer(acLease.SpeakerPlayerId); speaker && speaker->IsCampaignReady())
            speaker->Send(notify);
}

void DialogueService::Release(entt::entity aActor) noexcept
{
    if (const auto* lease = m_world.try_get<DialogueLeaseComponent>(aActor))
    {
        Broadcast(aActor, *lease, false);
        m_world.remove<DialogueLeaseComponent>(aActor);
    }
}

void DialogueService::OnRequest(const PacketEvent<RequestDialogueLease>& acPacket) noexcept
{
    const auto& request = acPacket.Packet;
    Player* speaker = acPacket.pPlayer;
    const uint64_t now = GameServer::Get()->GetTick();
    if (!speaker || !speaker->IsCampaignReady() || !request.IsValid() ||
        (request.Tick > now && request.Tick - now > 2000) || (now > request.Tick && now - request.Tick > RequestDialogueLease::kDurationMs))
        return;
    auto& state = m_speakers[speaker->GetId()];
    if (request.Sequence <= state.Sequence)
        return;
    state.Sequence = request.Sequence;
    const auto actor = static_cast<entt::entity>(request.ServerId);
    if (!m_world.valid(actor))
        return;
    auto* lease = m_world.try_get<DialogueLeaseComponent>(actor);
    if (!request.Active)
    {
        if (lease && lease->SpeakerPlayerId == speaker->GetId() && lease->OwnershipEpoch == request.OwnershipEpoch)
            Release(actor);
        return;
    }
    const auto* owner = m_world.try_get<OwnerComponent>(actor);
    if (!CanTalk(speaker, actor) || !owner || owner->OwnershipEpoch != request.OwnershipEpoch ||
        (state.LastRefresh != 0 && now - state.LastRefresh < 200))
        return;
    state.LastRefresh = now;
    if (lease && lease->Deadline <= now)
    {
        Release(actor);
        lease = nullptr;
    }
    if (lease && lease->SpeakerPlayerId != speaker->GetId())
        return; // Another live conversation never steals the current holder.
    Vector<entt::entity> previous;
    for (const auto other : m_world.view<DialogueLeaseComponent>())
        if (other != actor && m_world.get<DialogueLeaseComponent>(other).SpeakerPlayerId == speaker->GetId())
            previous.push_back(other);
    for (const auto other : previous)
        Release(other);
    DialogueLeaseComponent updated{speaker->GetId(), owner->GetOwner()->GetId(), request.OwnershipEpoch, now + RequestDialogueLease::kDurationMs};
    m_world.emplace_or_replace<DialogueLeaseComponent>(actor, updated);
    Broadcast(actor, updated, true);
}

void DialogueService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t now = GameServer::Get()->GetTick();
    if (now < m_nextUpdate)
        return;
    m_nextUpdate = now + 100;
    Vector<entt::entity> expired;
    for (const auto actor : m_world.view<DialogueLeaseComponent>())
    {
        const auto& lease = m_world.get<DialogueLeaseComponent>(actor);
        const auto* owner = m_world.try_get<OwnerComponent>(actor);
        if (lease.Deadline <= now || !owner || !owner->GetOwner() || owner->OwnershipEpoch != lease.OwnershipEpoch ||
            owner->GetOwner()->GetId() != lease.OwnerPlayerId || !CanTalk(FindPlayer(lease.SpeakerPlayerId), actor))
            expired.push_back(actor);
    }
    for (const auto actor : expired)
        Release(actor);
    for (auto it = m_speakers.begin(); it != m_speakers.end();)
        if (!FindPlayer(it->first))
            it = m_speakers.erase(it);
        else
            ++it;
}
