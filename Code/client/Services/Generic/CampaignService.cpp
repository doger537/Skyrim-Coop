#include <TiltedOnlinePCH.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
#include <Services/WorldCheckpointService.h>
#include <Games/TES.h>
#include <Forms/TESQuest.h>
#include <Structs/QuestSyncPolicy.h>
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

bool QuestService::IsSharedQuest(TESQuest* apQuest, const GameId& acId) const noexcept
{
    if (!apQuest || IsNonSyncableQuest(apQuest))
        return false;
    const uint8_t type = static_cast<uint8_t>(apQuest->type);
    const bool allowMisc = m_checkpoint.AllowMisc ||
        std::find(m_checkpoint.MiscQuests.begin(), m_checkpoint.MiscQuests.end(), acId) != m_checkpoint.MiscQuests.end();
    return QuestSyncPolicy::IsShared(QuestSyncPolicy::Classify(acId, type), type, allowMisc);
}

bool QuestService::SeedCampaign(uint32_t& aCount, String& aError) noexcept
{
    Vector<QuestJournal> seed;
    for (TESQuest* quest : ModManager::Get()->quests)
    {
        if (!quest)
            continue;
        QuestJournal journal;
        const bool mapped = m_world.GetModSystem().GetServerModId(quest->formID, journal.Id);
        if (!IsSharedQuest(quest, journal.Id))
            continue;
        const auto local = Observe(quest);
        journal.Type = static_cast<uint8_t>(quest->type);
        if (!NeedsSharedJournal(journal.Id, journal.Type, local))
            continue;
        if (!mapped)
        {
            aError = "Cannot map a progressed quest to the pinned campaign content";
            return false;
        }
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

bool QuestService::CheckCampaign(bool aApply, String& aError, bool aReportServer) noexcept
{
    aError.clear();
    if (!m_checkpointComplete || m_journals.size() != m_checkpoint.Count)
    {
        aError = "Shared quest snapshot is incomplete";
        return false;
    }
    using enum QuestAdmissionProblem;
    size_t failures = 0;
    bool sendDetails = aReportServer;
    RequestCampaignReady response;
    response.Nonce = m_checkpoint.Nonce;
    response.Revision = m_checkpoint.Revision;
    const auto report = [&](GameId aId, uint8_t aType, TESQuest* apQuest, const LocalQuestState& acLocal,
                            const QuestJournal* apJournal, QuestAdmissionProblem aProblem)
    {
        QuestAdmissionDiff diff;
        diff.Id = aId;
        diff.Type = aType;
        diff.LocalForm = apQuest ? apQuest->formID : 0;
        const char* pEditor = apQuest ? apQuest->idName.AsAscii() : nullptr;
        if (pEditor)
            diff.EditorId.assign(pEditor, std::min(std::strlen(pEditor), QuestAdmissionDiff::kMaxEditorId));
        diff.Local = acLocal;
        diff.Problem = aProblem;
        spdlog::warn("Campaign quest admission revision={} {}", m_checkpoint.Revision, DescribeQuestAdmission(diff, apJournal));
        if (++failures <= QuestAdmissionDiff::kMaxServerDetails && sendDetails && diff.IsValid())
        {
            response.State = RequestCampaignReady::Detail;
            response.Diff = std::move(diff);
            sendDetails = m_world.GetTransport().Send(response);
        }
    };
    Vector<QuestReplayPlan> plans(m_journals.size());
    std::set<uint32_t> known;
    for (size_t i = 0; i < m_journals.size(); ++i)
    {
        const auto& message = m_journals[i];
        const auto& journal = message.Data;
        const uint32_t form = m_world.GetModSystem().GetGameId(journal.Id);
        TESQuest* quest = Cast<TESQuest>(TESForm::GetById(form));
        const auto local = Observe(quest);
        auto& plan = plans[i];
        const bool unique = form && known.insert(form).second;
        if (!unique || !IsSharedQuest(quest, journal.Id) || static_cast<uint8_t>(quest->type) != journal.Type)
            plan.Problem = MissingOrUnsupported;
        else if (m_checkpoint.StartupDiagnosticOnly)
        {
            if (!PlanQuestReplay(journal, {}, false, local, plan, false))
            {
                QuestReplayPlan candidate;
                if (PlanQuestReplay(journal, {}, false, local, candidate))
                    spdlog::info("Campaign quest admission diagnostic-only: audited history candidate QuestID={:08X}:{:08X}",
                                 journal.Id.ModId, journal.Id.BaseId);
            }
        }
        else if (PlanQuestReplay(journal, message.ReplayStages, message.ReplayLifecycle, local, plan) && !aApply && !plan.Apply.empty())
            plan.Problem = ChangedDuringAdmission;
        if (plan.Problem != None)
            report(journal.Id, journal.Type, quest, local, &journal, plan.Problem);
    }
    for (TESQuest* quest : ModManager::Get()->quests)
    {
        if (!quest || known.contains(quest->formID))
            continue;
        GameId id{};
        const bool mapped = m_world.GetModSystem().GetServerModId(quest->formID, id);
        if (!IsSharedQuest(quest, id))
            continue;
        const auto local = Observe(quest);
        const uint8_t type = static_cast<uint8_t>(quest->type);
        if (NeedsSharedJournal(id, type, local))
            report(id, type, quest, local, nullptr, mapped ? LocalOnly : Unmapped);
    }
    if (failures)
    {
        if (sendDetails)
        {
            response.State = RequestCampaignReady::Refused;
            response.SeedCount = static_cast<uint32_t>(std::min<size_t>(failures, 4095));
            m_waitingForQuestRefusal = m_world.GetTransport().Send(response);
        }
        aError = fmt::format("{} incompatible quest(s). Full report in SkyrimTogetherReborn/logs/tp_client.log (Campaign quest admission)", failures).c_str();
        return false;
    }

    size_t count = 0;
    for (const auto& plan : plans)
        count += plan.Apply.size();
    if (count && m_world.ctx().at<WorldCheckpointService>().IsFrozen())
    {
        aError = "A restored paired save still needs quest replay. Load the matching paired checkpoint";
        return false;
    }
    if (count > kMaxQueuedUpdates)
    {
        aError = "The campaign requires too many quest updates to replay safely. Load a matching paired save";
        return false;
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
    if (!CheckCampaign(false, aError, !apAccepted))
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
        m_waitingForQuestRefusal = false;
        m_journals.clear();
        m_cursors.clear();
        m_questUpdates.clear();
        m_admissionReplayPending = false;
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
    if (m_checkpoint.SeedOwner)
    {
        if (!SeedCampaign(count, error))
        {
            m_world.GetTransport().FailCampaign(error);
            return;
        }
        RequestCampaignReady ready;
        ready.Nonce = m_checkpoint.Nonce;
        ready.Revision = m_checkpoint.Revision;
        ready.SeedCount = count;
        m_world.GetTransport().Send(ready);
        return;
    }
    if (!CheckCampaign(true, error, true))
    {
        if (!m_waitingForQuestRefusal)
            m_world.GetTransport().FailCampaign(error);
        return;
    }
    if (!m_questUpdates.empty())
        m_admissionReplayPending = true;
    else
        FinishCampaignAdmission();
}

void QuestService::FinishCampaignAdmission() noexcept
{
    String error;
    if (!ConfirmCampaign(error))
    {
        if (!m_waitingForQuestRefusal)
            m_world.GetTransport().FailCampaign(error);
        return;
    }
    RequestCampaignReady ready;
    ready.Nonce = m_checkpoint.Nonce;
    ready.Revision = m_checkpoint.Revision;
    m_world.GetTransport().Send(ready);
}

bool QuestService::SharedRun(uint32_t aFormId, GameId& aQuest, uint32_t& aRun) const noexcept
{
    auto* quest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!m_checkpointComplete || !m_world.GetModSystem().GetServerModId(aFormId, aQuest) || !IsSharedQuest(quest, aQuest)) return false;
    // A fragment can begin while the incoming stage is still being verified.
    // Its durable grant belongs to the authoritative run of that update.
    for (const auto& pending : m_questUpdates)
    {
        const auto& update = pending.Update;
        if (pending.Verification.Deadline && update.Revision && update.Id == aQuest &&
            update.WorldEpoch == m_world.GetTransport().GetWorldEpoch())
        {
            aRun = update.Epoch;
            return true;
        }
    }
    const auto it = m_cursors.find(aFormId);
    const auto policy = QuestSyncPolicy::Classify(aQuest, static_cast<uint8_t>(quest->type));
    if (policy.Type == QuestSyncPolicy::Category::Radiant && QuestSyncPolicy::IsPreparation(policy, Observe(quest).Reached))
        return false;
    aRun = it == m_cursors.end() ? 1 : it->second.Epoch;
    return aRun != 0;
}
void QuestService::SendQuestUpdate(uint32_t aFormId, uint16_t aStage, uint8_t aStatus, uint8_t aType) noexcept
{
    if (!m_world.GetTransport().IsConnected())
        return;
    auto* quest = Cast<TESQuest>(TESForm::GetById(aFormId));
    // The server accepts a guest echo only if its exact change is already in
    // the authoritative journal. Asynchronous replay fragments can emit an
    // event after ScopedQuestOverride ended; they must not become proposals.
    RequestQuestUpdate update;
    if (!m_world.GetModSystem().GetServerModId(aFormId, update.Id) || !IsSharedQuest(quest, update.Id))
        return;
    auto& cursor = m_cursors[aFormId];
    const auto policy = QuestSyncPolicy::Classify(update.Id, aType);
    const bool radiant = policy.Type == QuestSyncPolicy::Category::Radiant;
    if (radiant && aStatus == RequestQuestUpdate::Started)
    {
        cursor.RadiantStartPending = cursor.Stopped;
        return;
    }
    if (radiant && aStatus == RequestQuestUpdate::Stopped && (cursor.Stopped || (!cursor.Revision && aStage <= policy.PreparationThrough)))
    {
        cursor.RadiantStartPending = false;
        return;
    }
    if (radiant && aStatus == RequestQuestUpdate::StageUpdate && aStage <= policy.PreparationThrough)
        return;

    if (cursor.Stopped && (aStatus == RequestQuestUpdate::Started ||
                           (radiant && cursor.RadiantStartPending && aStatus == RequestQuestUpdate::StageUpdate)))
    {
        if (cursor.Epoch == UINT32_MAX)
        {
            m_world.GetTransport().FailCampaign("Quest run epoch exhausted");
            return;
        }
        ++cursor.Epoch;
        cursor.Stopped = false;
        cursor.RadiantStartPending = false;
    }
    if (aStatus == RequestQuestUpdate::Stopped)
        cursor.Stopped = true;
    update.Stage = aStage;
    update.Status = aStatus;
    update.ClientQuestType = aType;
    update.Epoch = cursor.Epoch;
    m_world.GetTransport().Send(update);
}
