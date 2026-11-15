#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Messages/NotifyQuestJournal.h>
#include <Messages/NotifyCampaignCheckpoint.h>

struct NotifyQuestUpdate;
struct DisconnectedEvent;
struct PartyLeftEvent;
struct UpdateEvent;

struct TESQuest;

/**
 * Syncs one world-wide quest event journal. Socket authentication is followed by
 * baseline validation before gameplay starts. Replay requires a pinned adapter;
 * this journal does not serialize Papyrus VM state, aliases or rewards.
 * Existing scene guards defer live stages checked by the party leader's scenes.
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>,
                           BSTEventSink<TESQuestStageEvent>,
                           BSTEventSink<TESQuestStageItemDoneEvent>,
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

    struct HeldStages
    {
        // Member stages not applied yet, sorted
        Vector<uint16_t> Stages{};
        // A stage per log entry of the applied stages whose fragment hasn't run yet
        Vector<uint16_t> PendingItems{};
    };

private:
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;

    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageItemDoneEvent*, const EventDispatcher<TESQuestStageItemDoneEvent>*) override;
    BSTEventResult OnEvent(const TESSceneEvent*, const EventDispatcher<TESSceneEvent>*) override;
    BSTEventResult OnEvent(const TESSceneActionEvent*, const EventDispatcher<TESSceneActionEvent>*) override;
    BSTEventResult OnEvent(const TESScenePhaseEvent*, const EventDispatcher<TESScenePhaseEvent>*) override;
    BSTEventResult OnEvent(const TESTopicInfoEvent*, const EventDispatcher<TESTopicInfoEvent>*) override;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void OnCampaignCheckpoint(const NotifyCampaignCheckpoint&) noexcept;
    void OnQuestJournal(const NotifyQuestJournal&) noexcept;
    bool CheckCampaign(bool aApply, String& aError) noexcept;
    bool SeedCampaign(uint32_t& aCount, String& aError) noexcept;
    bool IsSharedQuest(TESQuest* apQuest) const noexcept;
    void SendQuestUpdate(uint32_t aFormId, uint16_t aStage, uint8_t aStatus, uint8_t aType) noexcept;
    void HoldStage(TESQuest* apQuest, uint16_t aStage) noexcept;
    // The quest's held stages, while it has stages to apply or fragments to wait for. Drops the stages that can't be
    // applied anymore.
    HeldStages* GetHeldStages(uint32_t aQuestFormId, TESQuest* apQuest) noexcept;
    void NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept;

    World& m_world;
    // Keyed by quest form ID
    Map<uint32_t, HeldStages> m_heldStages{};
    struct Cursor
    {
        uint32_t Epoch{1};
        uint64_t Revision{};
        bool Stopped{};
    };
    Map<uint32_t, Cursor> m_cursors;
    NotifyCampaignCheckpoint m_checkpoint;
    Vector<NotifyQuestJournal> m_journals;
    bool m_checkpointComplete{};

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_checkpointConnection;
    entt::scoped_connection m_journalConnection;
};
