#include <TiltedOnlinePCH.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
#include <Forms/TESQuest.h>
#include <ModCompat/AlternateStart.h>
#include <Messages/RequestCampaignReady.h>
#include <Messages/RequestCampaignSeed.h>
#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Structs/QuestReplayPlan.h>
#include <set>

namespace
{
LocalQuestState Observe(TESQuest* apQuest)
{
    LocalQuestState state;
    if (!apQuest)
        return state;
    state.Exists = true;
    state.Stopped = apQuest->IsStopped();
    for (TESQuest::Stage* stage : apQuest->stages)
        if (stage && stage->IsDone() && std::find(state.Reached.begin(), state.Reached.end(), stage->stageIndex) == state.Reached.end())
            state.Reached.push_back(stage->stageIndex);
    if (apQuest->currentStage && std::find(state.Reached.begin(), state.Reached.end(), apQuest->currentStage) == state.Reached.end())
        state.Reached.push_back(apQuest->currentStage);
    return state;
}
} // namespace

bool QuestService::IsSharedQuest(TESQuest* apQuest) const noexcept
{
    if (!apQuest || IsNonSyncableQuest(apQuest))
        return false;
    if (apQuest->type != TESQuest::Type::None && apQuest->type != TESQuest::Type::Miscellaneous)
        return true;
    if (m_checkpoint.AllowMisc)
        return true;
    GameId id{};
    return m_world.GetModSystem().GetServerModId(apQuest->formID, id) &&
           std::find(m_checkpoint.MiscQuests.begin(), m_checkpoint.MiscQuests.end(), id) != m_checkpoint.MiscQuests.end();
}

bool QuestService::SeedCampaign(uint32_t& aCount, String& aError) noexcept
{
    Vector<QuestJournal> seed;
    for (TESQuest* quest : ModManager::Get()->quests)
    {
        if (!IsSharedQuest(quest))
            continue;
        const auto local = Observe(quest);
        if (local.Stopped && local.Reached.empty())
            continue;
        QuestJournal journal;
        if (!m_world.GetModSystem().GetServerModId(quest->formID, journal.Id))
        {
            aError = "Cannot map a progressed quest to the pinned campaign content";
            return false;
        }
        journal.Type = static_cast<uint8_t>(quest->type);
        journal.Started = true;
        journal.Stopped = local.Stopped;
        journal.Events.push_back({RequestQuestUpdate::Started, 0, true});
        for (uint16_t stage : local.Reached)
            journal.Events.push_back({RequestQuestUpdate::StageUpdate, stage, true});
        if (local.Stopped)
            journal.Events.push_back({RequestQuestUpdate::Stopped, quest->currentStage, true});
        if (!journal.IsValid() || seed.size() >= 4095)
        {
            aError = "Campaign baseline exceeds the supported quest or stage count";
            return false;
        }
        seed.push_back(std::move(journal));
    }
    // Validate the entire observation before uploading any part of the baseline.
    for (const auto& journal : seed)
    {
        RequestCampaignSeed message;
        message.Nonce = m_checkpoint.Nonce;
        message.Data = journal;
        if (!m_world.GetTransport().Send(message))
        {
            aError = "Connection closed while uploading the shared campaign baseline";
            return false;
        }
    }
    aCount = static_cast<uint32_t>(seed.size());
    return true;
}

bool QuestService::CheckCampaign(bool aApply, String& aError) noexcept
{
    if (!m_checkpointComplete || m_journals.size() != m_checkpoint.Count)
    {
        aError = "Shared quest snapshot is incomplete";
        return false;
    }
    Vector<QuestReplayPlan> plans;
    std::set<uint32_t> known;
    for (const auto& message : m_journals)
    {
        const uint32_t form = m_world.GetModSystem().GetGameId(message.Data.Id);
        TESQuest* quest = Cast<TESQuest>(TESForm::GetById(form));
        QuestReplayPlan plan;
        if (!IsSharedQuest(quest) || !known.insert(form).second || !PlanQuestReplay(message.Data, message.ReplayStages, message.ReplayLifecycle, Observe(quest), plan))
        {
            aError = "Quest " + std::to_string(message.Data.Id.LogFormat()).c_str() + ": " +
                     (plan.Error.empty() ? String("Missing, duplicate or unsupported quest in the shared campaign") : plan.Error);
            return false;
        }
        if (!aApply && !plan.Apply.empty())
        {
            aError = "Quest state changed during admission. Load a matching campaign save and reconnect";
            return false;
        }
        plans.push_back(std::move(plan));
    }
    for (TESQuest* quest : ModManager::Get()->quests)
    {
        if (!IsSharedQuest(quest) || known.contains(quest->formID))
            continue;
        const auto local = Observe(quest);
        if (!local.Stopped || !local.Reached.empty())
        {
            aError = "Local save has quest progress outside the shared campaign. Load a matching campaign save";
            return false;
        }
    }
    // No engine writes until every quest and every proposed replay has passed validation.
    if (aApply)
        for (size_t i = 0; i < plans.size(); ++i)
            for (const auto& event : plans[i].Apply)
            {
                NotifyQuestUpdate update;
                update.Id = m_journals[i].Data.Id;
                update.Stage = event.Stage;
                update.Status = event.Status;
                update.ClientQuestType = m_journals[i].Data.Type;
                OnQuestUpdate(update);
            }
    return true;
}

bool QuestService::ConfirmCampaign(String& aError, const NotifyCampaignCheckpoint* apAccepted) noexcept
{
    if (apAccepted &&
        (apAccepted->Nonce != m_checkpoint.Nonce || apAccepted->Revision != m_checkpoint.Revision || apAccepted->Count != m_checkpoint.Count || m_checkpoint.BaselinePending))
    {
        aError = "Accepted campaign checkpoint differs from the validated snapshot";
        return false;
    }
    if (!CheckCampaign(false, aError))
        return false;
    for (const auto& message : m_journals)
    {
        const uint32_t form = m_world.GetModSystem().GetGameId(message.Data.Id);
        m_cursors[form] = {message.Data.Epoch, message.Revision, message.Data.Stopped};
    }
    return true;
}

void QuestService::OnQuestJournal(const NotifyQuestJournal& acMessage) noexcept
{
    if (m_checkpointComplete || !m_checkpoint.Nonce || acMessage.Nonce != m_checkpoint.Nonce || !acMessage.Data.IsValid() || acMessage.Revision > m_checkpoint.Revision ||
        !acMessage.Revision || m_journals.size() >= m_checkpoint.Count ||
        std::any_of(m_journals.begin(), m_journals.end(), [&](const auto& old) { return old.Data.Id == acMessage.Data.Id; }))
    {
        m_world.GetTransport().FailCampaign("Invalid or duplicate shared quest journal");
        return;
    }
    m_journals.push_back(acMessage);
}

void QuestService::OnCampaignCheckpoint(const NotifyCampaignCheckpoint& acMessage) noexcept
{
    if (acMessage.State == NotifyCampaignCheckpoint::Begin)
    {
        m_checkpoint = acMessage;
        m_checkpointComplete = false;
        m_journals.clear();
        m_cursors.clear();
        return;
    }
    if (acMessage.State != NotifyCampaignCheckpoint::End || m_checkpointComplete || acMessage.Nonce != m_checkpoint.Nonce || acMessage.Revision != m_checkpoint.Revision ||
        acMessage.Count != m_checkpoint.Count || acMessage.SeedOwner != m_checkpoint.SeedOwner)
    {
        m_world.GetTransport().FailCampaign("Shared quest snapshot boundaries differ");
        return;
    }
    m_checkpointComplete = true;
    if (m_checkpoint.BaselinePending && !m_checkpoint.SeedOwner)
        return; // Another authenticated participant owns the one-time baseline lease.
    String error;
    uint32_t count = 0;
    if (m_checkpoint.SeedOwner ? !SeedCampaign(count, error) : (!CheckCampaign(true, error) || !ConfirmCampaign(error)))
    {
        m_world.GetTransport().FailCampaign(error);
        return;
    }
    RequestCampaignReady ready;
    ready.Nonce = m_checkpoint.Nonce;
    ready.Revision = m_checkpoint.Revision;
    ready.SeedCount = count;
    m_world.GetTransport().Send(ready);
}

void QuestService::SendQuestUpdate(uint32_t aFormId, uint16_t aStage, uint8_t aStatus, uint8_t aType) noexcept
{
    if (!m_world.GetTransport().IsConnected())
        return;
    auto* quest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!IsSharedQuest(quest))
        return;
    RequestQuestUpdate update;
    if (!m_world.GetModSystem().GetServerModId(aFormId, update.Id))
        return;
    auto& cursor = m_cursors[aFormId];
    if (aStatus == RequestQuestUpdate::Started && cursor.Stopped)
    {
        if (cursor.Epoch == UINT32_MAX)
        {
            m_world.GetTransport().FailCampaign("Quest run epoch exhausted");
            return;
        }
        ++cursor.Epoch;
        cursor.Stopped = false;
    }
    if (aStatus == RequestQuestUpdate::Stopped)
        cursor.Stopped = true;
    update.Stage = aStage;
    update.Status = aStatus;
    update.ClientQuestType = aType;
    update.Epoch = cursor.Epoch;
    m_world.GetTransport().Send(update);
}
