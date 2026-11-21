#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Services/QuestService.h>
#include <ModCompat/AlternateStart.h>
#include <Services/ImguiService.h>
#include <Services/OverlayService.h>
#include <Services/TransportService.h>
#include <Services/WorldCheckpointService.h>

#include <OverlayApp.hpp>

#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Structs/QuestSyncPolicy.h>

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&QuestService::OnDisconnected>(this);
    m_checkpointConnection = aDispatcher.sink<NotifyCampaignCheckpoint>().connect<&QuestService::OnCampaignCheckpoint>(this);
    m_journalConnection = aDispatcher.sink<NotifyQuestJournal>().connect<&QuestService::OnQuestJournal>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);

    // Game events:
    // Starts, stops and stages are sent from TESQuestStartStopEvent and TESQuestStageEvent, which fire while the quest
    // changes. They are sent only after campaign admission.
    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
    pEventList->sceneEvent.RegisterSink(this);
    pEventList->sceneActionEvent.RegisterSink(this);
    pEventList->scenePhaseEvent.RegisterSink(this);
    pEventList->topicInfoEvent.RegisterSink(this);
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // TODO: this should be followed with whatever the quest leader selected
    /*
    // deselect any active quests
    auto* pPlayer = PlayerCharacter::Get();
    for (auto& objective : pPlayer->objectives)
    {
        if (auto* pQuest = objective.instance->quest)
            pQuest->SetActive(false);
    }
    */
}

void QuestService::OnUpdate(const UpdateEvent&) noexcept
{
    TickQuestUpdates();
    if (m_admissionReplayPending && m_questUpdates.empty())
    {
        m_admissionReplayPending = false;
        FinishCampaignAdmission();
    }
}

void QuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_questUpdates.clear();
    m_admissionReplayPending = false;
    m_waitingForQuestRefusal = false;
    m_cursors.clear();
    m_journals.clear();
    m_checkpointComplete = false;
    m_checkpoint = {};
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    // The quest only failed to start here (e.g. a required alias didn't fill), others may be running it
    if (apEvent->started && apEvent->failed)
        return BSTEventResult::kOk;

    if (!m_world.GetTransport().IsConnected())
    {
        if (apEvent->started)
            NotifyOverlayOfQuestUpdate(apEvent->formId);
        return BSTEventResult::kOk;
    }

    spdlog::info("Quest start/stop event: {:X}", apEvent->formId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(
                    __FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}", Id.LogFormat(), pQuest->currentStage,
                    pQuest->IsStopped() ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started, static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                    pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().QueueGameplay(
            [this, generation = m_world.GetTransport().GetConnectionGeneration(), formId = pQuest->formID, stageId = pQuest->currentStage, stopped = pQuest->IsStopped(),
             type = pQuest->type]()
            {
                if (generation == m_world.GetTransport().GetConnectionGeneration())
                    SendQuestUpdate(formId, stageId, stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started, static_cast<uint8_t>(type));
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    if (!m_world.GetTransport().IsConnected())
    {
        NotifyOverlayOfQuestUpdate(apEvent->formId);
        return BSTEventResult::kOk;
    }

    spdlog::info("Quest stage event: {:X}, stage: {}", apEvent->formId, apEvent->stageId);

    // there is no reason to even fetch the quest object, since the event provides everything already....
    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(
                    __FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}", Id.LogFormat(), pQuest->currentStage,
                    RequestQuestUpdate::StageUpdate, static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().QueueGameplay(
            [this, generation = m_world.GetTransport().GetConnectionGeneration(), formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
            {
                if (generation == m_world.GetTransport().GetConnectionGeneration())
                    SendQuestUpdate(formId, stageId, RequestQuestUpdate::StageUpdate, static_cast<uint8_t>(type));
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESSceneEvent* apEvent, const EventDispatcher<TESSceneEvent>*)
{
    spdlog::debug("Scene {:X} {}", apEvent->sceneFormId, apEvent->type == 0 ? "begin" : "end");
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESSceneActionEvent* apEvent, const EventDispatcher<TESSceneActionEvent>*)
{
    spdlog::debug("Scene {:X} action {} done, quest: {:X}, alias: {}", apEvent->sceneFormId, apEvent->actionIndex, apEvent->questFormId, apEvent->aliasId);
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESScenePhaseEvent* apEvent, const EventDispatcher<TESScenePhaseEvent>*)
{
    if (!spdlog::should_log(spdlog::level::debug))
        return BSTEventResult::kOk;

    BGSScene* pScene = Cast<BGSScene>(TESForm::GetById(apEvent->sceneFormId));
    if (!pScene)
        return BSTEventResult::kOk;

    // A phase without completion conditions ends once every action running in it is complete
    String actions;
    for (BGSSceneAction* pAction : pScene->actions)
    {
        if (!pAction || !pAction->IsInPhase(apEvent->phaseIndex))
            continue;

        constexpr std::array cTypeNames{"dialogue", "package", "timer"};
        const uint32_t cType = static_cast<uint32_t>(pAction->GetType());
        actions += spdlog::fmt_lib::format(
            " [{} {} alias {} {}]", pAction->GetIndex(), cType < cTypeNames.size() ? cTypeNames[cType] : "unknown", pAction->actorId,
            pAction->IsComplete() ? "complete" : "running");
    }

    spdlog::debug("Scene {:X} phase {} {}, actions:{}", apEvent->sceneFormId, apEvent->phaseIndex, apEvent->type == 0 ? "begin" : "end", actions);
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESTopicInfoEvent* apEvent, const EventDispatcher<TESTopicInfoEvent>*)
{
    spdlog::debug(
        "Dialogue line {:X} {}, speaker: {:X}", apEvent->uiTopicInfoFormID, apEvent->eType == 0 ? "begin" : "end", apEvent->pSpeakerRef ? apEvent->pSpeakerRef->formID : 0);
    return BSTEventResult::kOk;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& acUpdate) noexcept
{
    if (!acUpdate.IsValid() || QuestSyncPolicy::Classify(acUpdate.Id, acUpdate.ClientQuestType).Type == QuestSyncPolicy::Category::Local)
        return;

    const uint32_t form = m_world.GetModSystem().GetGameId(acUpdate.Id);
    if (AlternateStart::IsPersonalQuest(form))
        return;
    if (acUpdate.Revision)
    {
        const auto cursor = m_cursors.find(form);
        if (cursor != m_cursors.end() && (acUpdate.Epoch < cursor->second.Epoch || acUpdate.Revision <= cursor->second.Revision))
            return;
    }
    if (std::any_of(m_questUpdates.begin(), m_questUpdates.end(), [&](const auto& aEntry) { return aEntry.Update == acUpdate; }))
        return;
    if (m_questUpdates.size() >= kMaxQueuedUpdates)
    {
        m_questUpdates.clear();
        m_admissionReplayPending = false;
        m_world.GetTransport().FailCampaign("The remote quest update queue is full. Reload the matching paired save");
        return;
    }
    m_questUpdates.push_back({acUpdate});
}

void QuestService::TickQuestUpdates() noexcept
{
    if (m_questUpdates.empty() || m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    auto& queued = m_questUpdates.front();
    const auto& update = queued.Update;
    if (update.Revision && update.WorldEpoch != m_world.GetTransport().GetWorldEpoch())
    {
        m_questUpdates.pop_front();
        return;
    }
    const uint32_t form = m_world.GetModSystem().GetGameId(update.Id);
    if (update.Revision)
    {
        const auto cursor = m_cursors.find(form);
        if (cursor != m_cursors.end() && (update.Epoch < cursor->second.Epoch || update.Revision <= cursor->second.Revision))
        {
            m_questUpdates.pop_front();
            return;
        }
        if (update.AcknowledgementOnly)
        {
            // The sender already observed this local engine event before requesting it.
            m_cursors[form] = {update.Epoch, update.Revision, update.RunStopped};
            m_questUpdates.pop_front();
            return;
        }
    }
    auto* quest = Cast<TESQuest>(TESForm::GetById(form));
    const auto fail = [&](const char* apReason)
    {
        spdlog::error("Quest application failed QuestID={:08X}:{:08X} LocalFormID={:08X} Stage={} Status={} Epoch={} Revision={} NativeResult={} Reason={}",
                      update.Id.ModId, update.Id.BaseId, form, update.Stage, update.Status, update.Epoch, update.Revision, queued.NativeResult, apReason);
        m_questUpdates.clear();
        m_admissionReplayPending = false;
        m_world.GetTransport().FailCampaign("Skyrim did not confirm a shared quest update. See the client log and reload the matching paired save");
    };
    if (!quest || static_cast<uint8_t>(quest->type) != update.ClientQuestType)
    {
        fail("quest is missing or its type differs");
        return;
    }

    const bool stageReached = update.Stage ? quest->IsStageReached(update.Stage) : quest->IsStageDone(0);
    bool reached = stageReached;
    if (update.Status == NotifyQuestUpdate::Stopped)
        reached = quest->IsStopped();
    else if (update.Status == NotifyQuestUpdate::Started)
        reached = !quest->IsStopped() && (!update.Stage || stageReached);

    switch (queued.Verification.Observe(GetTickCount64(), reached))
    {
    case QuestApplyConfirmation::Next::Confirm:
        if (update.Status == NotifyQuestUpdate::Started)
            quest->SetActive(true);
        if (update.Revision)
            m_cursors[form] = {update.Epoch, update.Revision, update.RunStopped};
        m_questUpdates.pop_front();
        break;
    case QuestApplyConfirmation::Next::Invoke:
    {
        ScopedQuestOverride guard;
        if (update.Status == NotifyQuestUpdate::Stopped)
            queued.NativeResult = StopQuest(form);
        else if (update.Status == NotifyQuestUpdate::Started && !update.Stage)
        {
            bool started{};
            queued.NativeResult = quest->EnsureQuestStarted(started, false) && started;
        }
        else
            queued.NativeResult = quest->ScriptSetStage(update.Stage);
        break;
    }
    case QuestApplyConfirmation::Next::Failed:
        fail("engine stage or lifecycle readback timed out");
        break;
    case QuestApplyConfirmation::Next::Wait:
        break;
    }
}

void QuestService::NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept
{
    if (!m_world.GetTransport().IsOnline())
        return;

    // Nobody around to party with, or the invite toast already prompts the player to join one.
    const auto remotePlayerView = m_world.view<PlayerComponent, RemoteComponent>();
    if (m_world.GetPartyService().HasPendingInvitations() || remotePlayerView.begin() == remotePlayerView.end())
        return;

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!pQuest || IsNonSyncableQuest(pQuest))
        return;

    // Hidden background quests and minor misc tasks are too noisy to prompt about
    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        return;

    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("questUpdated");
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        pQuest->SetActive(false);
        pQuest->SetStopped();
        return true;
    }

    return false;
}

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    constexpr uint32_t kVampireTransformation = 0x020071D0;
    return apQuest->stages.Empty() || apQuest->formID == kVampireTransformation ||
           QuestSyncPolicy::Classify(GameId(0, apQuest->formID), static_cast<uint8_t>(apQuest->type)).Type == QuestSyncPolicy::Category::Local ||
           AlternateStart::IsPersonalQuest(apQuest->formID);
}

void QuestService::DebugDumpQuests()
{
    auto& quests = ModManager::Get()->quests;
    for (TESQuest* pQuest : quests)
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, pQuest->idName.AsAscii());
}
