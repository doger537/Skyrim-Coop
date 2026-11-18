#include <TiltedOnlinePCH.h>
#include <Services/QuestService.h>
#include <Services/PartyService.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/RequestQuestDecision.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>

void QuestService::OnQuestProposal(const NotifyQuestProposal& acMessage) noexcept
{
    if (!acMessage.IsValid()) return;
    const uint64_t generation = m_world.GetTransport().GetConnectionGeneration();
    m_world.GetRunner().QueueGameplay([this, message = acMessage, generation]()
    {
        if (generation != m_world.GetTransport().GetConnectionGeneration() || !m_world.GetTransport().IsConnected() ||
            !m_world.GetPartyService().IsLeader() || message.Proposal <= m_latestProposal)
            return;
        m_latestProposal = message.Proposal;
        if (m_proposals.size() >= 16)
        {
            RequestQuestDecision rejected;
            rejected.Proposal = message.Proposal;
            m_world.GetTransport().Send(rejected);
            return;
        }
        m_proposals.push_back({message, generation, GetTickCount64() + 170000, false});
    });
}
void QuestService::TickProposals() noexcept
{
    if (m_proposals.empty()) return;
    auto& transport = m_world.GetTransport();
    auto& proposal = m_proposals.front();
    if (!transport.IsConnected() || proposal.ConnectionGeneration != transport.GetConnectionGeneration())
    {
        m_proposals.clear();
        return;
    }
    const auto& change = proposal.Change;
    const uint32_t formId = m_world.GetModSystem().GetGameId(change.Id);
    auto* quest = Cast<TESQuest>(TESForm::GetById(formId));
    const bool permitted = m_world.GetPartyService().IsLeader() && quest && !IsNonSyncableQuest(quest) &&
        static_cast<uint8_t>(quest->type) == change.QuestType;
    bool accepted{};
    if (permitted && !proposal.Applied)
    {
        // Reuse the existing scene-aware live quest application and
        // scoped echo guard. This frame never waits for the native VM or a peer.
        NotifyQuestUpdate update;
        update.Id = change.Id; update.Epoch = change.Epoch; update.Stage = change.Stage;
        update.Status = change.Status; update.ClientQuestType = change.QuestType;
        proposal.Applied = true;
        OnQuestUpdate(update);
        return; // Observe the result on a later runner frame, not the call's return.
    }
    if (permitted)
    {
        if (change.Status == NotifyQuestUpdate::Stopped)
            accepted = quest->IsStopped();
        else if (change.Status == NotifyQuestUpdate::Started)
            accepted = !quest->IsStopped() && (change.Stage == 0 || quest->IsStageReached(change.Stage));
        else
            accepted = quest->IsStageReached(change.Stage);
    }
    if (!accepted && permitted && GetTickCount64() < proposal.Deadline) return;
    RequestQuestDecision decision;
    decision.Proposal = change.Proposal;
    decision.Accepted = accepted;
    transport.Send(decision);
    m_proposals.pop_front();
}
