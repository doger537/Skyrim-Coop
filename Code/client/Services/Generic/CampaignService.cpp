#include <TiltedOnlinePCH.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
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

TiltedPhoques::Vector<uint16_t> JournalStages(const QuestJournal& acJournal)
{
    std::set<uint16_t> stages;
    for (const auto& event : acJournal.Events)
        if (event.Status == RequestQuestUpdate::StageUpdate || (event.Status == RequestQuestUpdate::Started && event.Stage != 0))
            stages.insert(event.Stage);
    return Vector<uint16_t>(stages.begin(), stages.end());
}

std::string StageList(Vector<uint16_t> aStages)
{
    std::sort(aStages.begin(), aStages.end());
    std::string result("[");
    for (uint16_t stage : aStages)
    {
        if (result.size() > 1)
            result += ',';
        result += std::to_string(stage);
    }
    result += ']';
    return result;
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

bool QuestService::CheckCampaign(bool aApply, String& aError) noexcept
{
    aError.clear();
    if (!m_checkpointComplete || m_journals.size() != m_checkpoint.Count)
    {
        aError = "Shared quest snapshot is incomplete";
        return false;
    }
    size_t failures = 0;
    const auto report = [&](GameId aId, uint8_t aType, TESQuest* apQuest, const LocalQuestState& acLocal,
                            const QuestJournal* apJournal, const String& acReason)
    {
        const char* pEditor = apQuest ? apQuest->idName.AsAscii() : nullptr;
        const auto category = QuestSyncPolicy::CategoryName(QuestSyncPolicy::Classify(aId, aType).Type);
        const auto local = !acLocal.Exists ? std::string("missing") :
            fmt::format("Stages={} Started={} (inferred) Stopped={} Epoch=unknown",
                        StageList(acLocal.Reached), !acLocal.Stopped || !acLocal.Reached.empty(), acLocal.Stopped);
        const auto server = !apJournal ? std::string("absent") :
            fmt::format("Stages={} Started={} Stopped={} Epoch={}",
                        StageList(JournalStages(*apJournal)), apJournal->Started, apJournal->Stopped, apJournal->Epoch);
        spdlog::warn("Campaign quest admission revision={} QuestID={:08X}:{:08X} LocalFormID={:08X} EditorID={} Type={} ({}) Server=[{}] Local=[{}] Reason={}",
                     m_checkpoint.Revision, aId.ModId, aId.BaseId, apQuest ? apQuest->formID : 0,
                     pEditor ? pEditor : "unresolved", aType, category, server, local, acReason);
        ++failures;
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
            plan.Error = "Missing, duplicate or unsupported quest, or quest type differs from the campaign";
        else if (PlanQuestReplay(journal, message.ReplayStages, message.ReplayLifecycle, local, plan) && !aApply && !plan.Apply.empty())
            plan.Error = "Quest changed during admission. Load a matching campaign save and reconnect";
        if (!plan.Error.empty())
            report(journal.Id, journal.Type, quest, local, &journal, plan.Error);
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
            report(id, type, quest, local, nullptr, mapped ? "Local quest progress is absent from the shared campaign" :
                   "Local quest progress could not be mapped to the shared content");
    }
    if (failures)
    {
        aError = fmt::format("{} incompatible quest(s). Full report in SkyrimTogetherReborn/logs/tp_client.log (Campaign quest admission)", failures).c_str();
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

bool QuestService::SharedRun(uint32_t aFormId, GameId& aQuest, uint32_t& aRun) const noexcept
{
    auto* quest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!m_checkpointComplete || !m_world.GetModSystem().GetServerModId(aFormId, aQuest) || !IsSharedQuest(quest, aQuest)) return false;
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
