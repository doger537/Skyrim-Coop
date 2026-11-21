#include <World.h>
#include <GameServer.h>
#include <Components.h>
#include <Services/RagdollService.h>
#include <Services/WorldCheckpointService.h>
#include <Messages/RequestRagdollPose.h>
#include <Messages/NotifyRagdollPose.h>
#include <algorithm>
#include <limits>
RagdollService::RagdollService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    m_poseConnection = aDispatcher.sink<PacketEvent<RequestRagdollPose>>().connect<&RagdollService::OnPose>(this);
}
void RagdollService::OnPose(const PacketEvent<RequestRagdollPose>& acPacket) noexcept
{
    const auto& state = acPacket.Packet.State;
    const auto entity = static_cast<entt::entity>(state.ServerId);
    if (!state.IsValid() || !acPacket.pPlayer->IsCampaignReady() || m_world.ctx().at<WorldCheckpointService>().IsFrozen() || !m_world.valid(entity)) return;
    const auto* owner = m_world.try_get<OwnerComponent>(entity);
    const auto* cell = m_world.try_get<CellIdComponent>(entity);
    const auto* character = m_world.try_get<CharacterComponent>(entity);
    if (!owner || !cell || !character || owner->OwnershipEpoch != state.OwnershipEpoch ||
        cell->Cell != state.Cell || cell->WorldSpaceId != state.WorldSpace || !m_world.GetCampaign().ValidForm(state.Cell) ||
        !m_world.GetCampaign().ValidForm(state.WorldSpace, true)) return;
    const uint64_t now = GameServer::Get()->GetTick();
    auto& current = m_world.get_or_emplace<RagdollComponent>(entity);
    if (state.Interaction)
    {
        auto* authority = owner->GetOwner();
        const auto playerEntity = acPacket.pPlayer->GetCharacter();
        const auto* playerMove = playerEntity ? m_world.try_get<MovementComponent>(*playerEntity) : nullptr;
        const auto* actorMove = m_world.try_get<MovementComponent>(entity);
        if (!authority || authority == acPacket.pPlayer || !authority->IsCampaignReady() || !playerMove ||
            !acPacket.pPlayer->GetParty().JoinedPartyId || acPacket.pPlayer->GetParty().JoinedPartyId != authority->GetParty().JoinedPartyId ||
            !cell->IsInRange(acPacket.pPlayer->GetCellComponent(), false) ||
            !current.Latest.IsValid() || current.OwnershipEpoch != state.OwnershipEpoch || now > current.ReceivedAt + 500 ||
            !current.Latest.Rig.SameGraph(state.Rig) || (actorMove && actorMove->Pose.Known && !actorMove->Pose.Ragdoll) ||
            (current.Interactor && current.Interactor != acPacket.pPlayer->GetId() && now < current.InteractionAt + 500) ||
            (current.Interactor == acPacket.pPlayer->GetId() && (state.Sequence <= current.InteractionSequence || now < current.InteractionAt + 40))) return;
        double nearest = std::numeric_limits<double>::max();
        for (size_t i = 0; i < state.Rig.Count; ++i)
        {
            double displacement{}, speed{}, spin{}, distance{};
            for (size_t j = 0; j < 3; ++j)
            {
                const double d = double(state.Rig.Bodies[i].Pose.Position[j]) - current.Latest.Rig.Bodies[i].Pose.Position[j];
                displacement += d * d;
                const double v = state.Rig.Bodies[i].Pose.LinearVelocity[j], w = state.Rig.Bodies[i].Pose.AngularVelocity[j];
                speed += v * v; spin += w * w;
                const double p = double(current.Latest.Rig.Bodies[i].Pose.Position[j]) - playerMove->Position[j]; distance += p * p;
            }
            if (displacement > 256. * 256. || speed > 4096. * 4096. || spin > 32. * 32.) return;
            nearest = std::min(nearest, distance);
        }
        if (nearest > 400. * 400.) return;
        current.Interactor = acPacket.pPlayer->GetId(); current.InteractionSequence = state.Sequence; current.InteractionAt = now;
        NotifyRagdollPose input; input.State = state;
        // Only the simulation owner receives guest input. Its next ordinary
        // capture is the canonical result; guest input is never broadcast as truth.
        authority->Send(input, TiltedPhoques::kUnreliableNoNagle);
        return;
    }
    if (!owner->IsCurrentOwner(acPacket.pPlayer, state.OwnershipEpoch)) return;
    if (current.OwnershipEpoch == state.OwnershipEpoch &&
        (state.Sequence <= current.Sequence || state.Tick <= current.Tick || now < current.ReceivedAt + 40)) return;
    if (current.OwnershipEpoch != state.OwnershipEpoch) current = {};
    current.OwnershipEpoch = state.OwnershipEpoch; current.Sequence = state.Sequence; current.Tick = state.Tick; current.ReceivedAt = now; current.Latest = state;
    NotifyRagdollPose notify; notify.State = state;
    for (Player* recipient : m_world.GetPlayerManager())
        if (recipient != acPacket.pPlayer && recipient->IsCampaignReady() && cell->IsInRange(recipient->GetCellComponent(), character->IsDragon()))
            recipient->Send(notify, TiltedPhoques::kUnreliableNoNagle);
}
