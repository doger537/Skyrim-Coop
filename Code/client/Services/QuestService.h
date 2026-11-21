#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Messages/NotifyQuestJournal.h>
#include <Messages/NotifyCampaignCheckpoint.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Structs/QuestApplyConfirmation.h>
#include <deque>

struct DisconnectedEvent;
struct UpdateEvent;

struct TESQuest;

/**
 * Syncs one world-wide quest event journal. Socket authentication is followed by
 * baseline validation before gameplay starts. Replay requires a pinned adapter;
 * this journal does not serialize Papyrus VM state, aliases or rewards.
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>,
                           BSTEventSink<TESQuestStageEvent>,
                           BSTEventSink<TESSceneEvent>,
                           BSTEventSink<TESSceneActionEvent>,
                           BSTEventSink<TESScenePhaseEvent>,
                           BSTEventSink<TESTopicInfoEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);
    bool ConfirmCampaign(String& aError, const NotifyCampaignCheckpoint* apAccepted = nullptr) noexcept;
    bool SharedRun(uint32_t aFormId, GameId& aQuest, uint32_t& aRun) const noexcept;

    bool HasUnconfirmedUpdates() const noexcept { return !m_questUpdates.empty(); }

private:
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;
    BSTEventResult OnEvent(const TESSceneEvent*, const EventDispatcher<TESSceneEvent>*) override;
    BSTEventResult OnEvent(const TESSceneActionEvent*, const EventDispatcher<TESSceneActionEvent>*) override;
    BSTEventResult OnEvent(const TESScenePhaseEvent*, const EventDispatcher<TESScenePhaseEvent>*) override;
    BSTEventResult OnEvent(const TESTopicInfoEvent*, const EventDispatcher<TESTopicInfoEvent>*) override;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void TickQuestUpdates() noexcept;
    void FinishCampaignAdmission() noexcept;
    static constexpr size_t kMaxQueuedUpdates = 256;
    struct QueuedQuest
    {
        NotifyQuestUpdate Update;
        QuestApplyConfirmation Verification;
        bool NativeResult{};
    };
    std::deque<QueuedQuest> m_questUpdates;
    bool m_admissionReplayPending{};
    void OnCampaignCheckpoint(const NotifyCampaignCheckpoint&) noexcept;
    void OnQuestJournal(const NotifyQuestJournal&) noexcept;
    bool CheckCampaign(bool aApply, String& aError, bool aReportServer) noexcept;
    bool SeedCampaign(uint32_t& aCount, String& aError) noexcept;
    bool IsSharedQuest(TESQuest* apQuest, const GameId& acId) const noexcept;
    void SendQuestUpdate(uint32_t aFormId, uint16_t aStage, uint8_t aStatus, uint8_t aType) noexcept;
    void NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept;

    World& m_world;
    struct Cursor
    {
        uint32_t Epoch{1};
        uint64_t Revision{};
        bool Stopped{};
        bool RadiantStartPending{};
    };
    Map<uint32_t, Cursor> m_cursors;
    NotifyCampaignCheckpoint m_checkpoint;
    Vector<NotifyQuestJournal> m_journals;
    bool m_checkpointComplete{};
    bool m_waitingForQuestRefusal{};

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_checkpointConnection;
    entt::scoped_connection m_journalConnection;
};
