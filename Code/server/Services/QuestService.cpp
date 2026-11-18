#include <GameServer.h>
#include <Components.h>
#include <World.h>
#include <Services/QuestService.h>
#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Setting.h>
#include <Messages/NotifyQuestProposal.h>
#include <Messages/RequestQuestDecision.h>
#include <Services/WorldCheckpointService.h>
#include <Events/UpdateEvent.h>
#include <algorithm>
#include <limits>

namespace
{
Console::Setting bEnableMiscQuestSync{"Gameplay:bEnableMiscQuestSync", "(Experimental) Syncs miscellaneous quests when possible", false};
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
    m_decisionConnection = aDispatcher.sink<PacketEvent<RequestQuestDecision>>().connect<&QuestService::OnDecision>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestService::OnUpdate>(this);
}

bool QuestService::AllowsMisc() const noexcept
{
    return bEnableMiscQuestSync;
}

void QuestService::FillPlayerJournal(Player& aPlayer) const noexcept
{
    auto& entries = aPlayer.GetQuestLogComponent().QuestContent.Entries;
    entries.clear();
    for (const auto& saved : m_world.GetCampaign().Quests())
    {
        if (saved.Data.Stopped)
            continue;
        uint16_t last = 0;
        for (const auto& event : saved.Data.Events)
            if (event.Status != RequestQuestUpdate::Stopped)
                last = event.Stage;
        entries.push_back({saved.Data.Id, last});
    }
}

bool QuestService::Commit(Player& aInitiator, const RequestQuestUpdate& message, const Campaign::QuestRunParticipants* apParticipants) noexcept
{
    auto& campaign = m_world.GetCampaign();
    const auto result = campaign.UpdateQuest(message.Id, message.Epoch, message.Status, message.Stage, message.ClientQuestType, AllowsMisc(),
        apParticipants);
    using Result = Campaign::State::QuestResult;
    if (result == Result::Ignored)
        return true;
    if (result == Result::StorageError)
    {
        GameServer::Get()->StopForWorldError(campaign.Error());
        return false;
    }
    if (result == Result::Invalid || result == Result::Conflict)
    {
        GameServer::Get()->RejectCampaign(aInitiator.GetConnectionId(), campaign.Error() + ". Load a matching campaign save before reconnecting");
        return false;
    }
    const auto* saved = campaign.FindQuest(message.Id);
    if (!saved)
        return false;
    NotifyQuestUpdate notify;
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    notify.Status = message.Status;
    notify.ClientQuestType = message.ClientQuestType;
    notify.Epoch = saved->Data.Epoch;
    notify.Revision = saved->Revision;
    notify.RunStopped = saved->Data.Stopped;
    if (result == Result::Applied)
    {
        for (Player* pPlayer : m_world.GetPlayerManager())
            if (pPlayer->IsCampaignReady())
                FillPlayerJournal(*pPlayer);
        GameServer::Get()->SendToPlayers(notify, &aInitiator);
    }
    // The sender already applied its local event. Acknowledging must not rerun its fragment.
    notify.AcknowledgementOnly = true;
    aInitiator.Send(notify);
    return true;
}

Player* QuestService::FindPlayer(uint32_t aId) const noexcept
{
    return m_world.GetPlayerManager().GetById(aId);
}
bool QuestService::ValidPair(const Proposal& acProposal) const noexcept
{
    auto* host = FindPlayer(acProposal.HostId);
    auto* initiator = FindPlayer(acProposal.InitiatorId);
    if (!host || !initiator || !host->IsCampaignReady() || !initiator->IsCampaignReady() ||
        host->GetPersistentCharacterId() != acProposal.HostCharacter ||
        initiator->GetPersistentCharacterId() != acProposal.Participants.Initiator ||
        acProposal.WorldEpoch != m_world.GetCampaign().WorldEpoch() || !m_world.GetPartyService().IsPlayerLeader(host))
        return false;
    const auto* party = m_world.GetPartyService().GetPlayerParty(initiator);
    return party && party->LeaderPlayerId == host->GetId() && party->Members.size() == 2 &&
        host->GetParty().JoinedPartyId == initiator->GetParty().JoinedPartyId;
}
void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acPacket) noexcept
{
    auto& initiator = *acPacket.pPlayer;
    const auto& change = acPacket.Packet;
    if (!change.IsValid())
    {
        GameServer::Get()->RejectCampaign(initiator.GetConnectionId(), "Malformed or truncated quest update");
        return;
    }
    auto* party = m_world.GetPartyService().GetPlayerParty(&initiator);
    Campaign::QuestRunParticipants participants;
    participants.Initiator = initiator.GetPersistentCharacterId();
    if (party && party->Members.size() == 2)
        for (size_t i = 0; i < 2; ++i) participants.Players[i] = party->Members[i]->GetPersistentCharacterId();
    if (!party || party->Members.size() != 2)
    {
        Commit(initiator, change, nullptr);
        return;
    }
    if (m_world.GetPartyService().IsPlayerLeader(&initiator))
    {
        // Native fragments can emit a second stage after the scoped override
        // ended. Do not let it steal the pending guest's first-run ownership.
        const auto pending = std::find_if(m_proposals.begin(), m_proposals.end(), [&](const auto& proposal)
        { return proposal.Change.Id == change.Id && proposal.Change.Epoch == change.Epoch; });
        if (pending != m_proposals.end())
        {
            if (pending->Change.Status == change.Status && pending->Change.Stage == change.Stage)
                return;
            if (pending->DeferredHostChanges.size() >= 64)
                GameServer::Get()->RejectCampaign(initiator.GetConnectionId(), "Too many derived quest changes while host approval is pending");
            else
                pending->DeferredHostChanges.push_back(change);
            return;
        }
        Commit(initiator, change, participants.IsValid() ? &participants : nullptr);
        return;
    }
    for (const auto& pending : m_proposals)
        if (pending.InitiatorId == initiator.GetId() && pending.Change == change)
            return;
    if (!participants.IsValid() || m_proposals.size() >= 64 || m_nextProposal == std::numeric_limits<uint64_t>::max())
    {
        GameServer::Get()->RejectCampaign(initiator.GetConnectionId(), "The bounded host quest approval queue is unavailable; reload a matching save");
        return;
    }
    Proposal proposal;
    proposal.Id = ++m_nextProposal;
    proposal.WorldEpoch = m_world.GetCampaign().WorldEpoch();
    proposal.InitiatorId = initiator.GetId();
    proposal.HostId = party->LeaderPlayerId;
    proposal.Participants = participants;
    proposal.Change = change;
    if (auto* host = FindPlayer(proposal.HostId)) proposal.HostCharacter = host->GetPersistentCharacterId();
    if (!ValidPair(proposal))
    {
        GameServer::Get()->RejectCampaign(initiator.GetConnectionId(), "The host is not ready to approve shared quest changes");
        return;
    }
    m_proposals.push_back(std::move(proposal));
}
void QuestService::FinishProposal(bool aAccepted) noexcept
{
    if (m_proposals.empty()) return;
    auto proposal = std::move(m_proposals.front());
    m_proposals.pop_front();
    auto* initiator = FindPlayer(proposal.InitiatorId);
    auto* host = FindPlayer(proposal.HostId);
    if (!aAccepted || !initiator || !Commit(*initiator, proposal.Change, &proposal.Participants))
    {
        if (initiator) GameServer::Get()->RejectCampaign(initiator->GetConnectionId(), "The host could not confirm this quest change. Reload a matching checkpoint");
        if (host && proposal.Sent) GameServer::Get()->RejectCampaign(host->GetConnectionId(), "A host quest proposal was interrupted. Reload a matching checkpoint before continuing");
        return;
    }
    if (host)
        for (const auto& change : proposal.DeferredHostChanges)
            if (!Commit(*host, change, &proposal.Participants)) break;
}
void QuestService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_proposals.empty()) return;
    auto& proposal = m_proposals.front();
    const uint64_t now = GameServer::Get()->GetTick();
    if (!ValidPair(proposal) || (proposal.Sent && now >= proposal.Deadline))
    {
        FinishProposal(false);
        return;
    }
    if (proposal.Sent || m_world.ctx().at<WorldCheckpointService>().IsFrozen()) return;
    auto& campaign = m_world.GetCampaign();
    const auto& change = proposal.Change;
    const auto preview = campaign.PreviewQuest(change.Id, change.Epoch, change.Status, change.Stage, change.ClientQuestType, AllowsMisc(), &proposal.Participants);
    using Result = Campaign::State::QuestResult;
    if (preview == Result::Ignored) { m_proposals.pop_front(); return; }
    if (preview == Result::Duplicate) { FinishProposal(true); return; }
    if (preview != Result::Applied) { FinishProposal(false); return; }
    NotifyQuestProposal notify;
    notify.Proposal = proposal.Id; notify.Id = change.Id; notify.Epoch = change.Epoch;
    notify.Stage = change.Stage; notify.Status = change.Status; notify.QuestType = change.ClientQuestType;
    proposal.Sent = true;
    proposal.Deadline = now + 180000;
    FindPlayer(proposal.HostId)->Send(notify);
}
void QuestService::OnDecision(const PacketEvent<RequestQuestDecision>& acPacket) noexcept
{
    if (!acPacket.Packet.IsValid() || m_proposals.empty()) return;
    const auto& proposal = m_proposals.front();
    if (!proposal.Sent || acPacket.Packet.Proposal != proposal.Id || acPacket.pPlayer->GetId() != proposal.HostId || !ValidPair(proposal)) return;
    FinishProposal(acPacket.Packet.Accepted);
}
