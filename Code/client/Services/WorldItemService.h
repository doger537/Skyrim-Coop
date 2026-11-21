#pragma once
#include <Messages/WorldItemMessages.h>
#include <WorldItemTimeline.h>
#include <Forms/WorldItemRegistry.h>
#include <Havok/PhysicsReplication.h>
#include <optional>
struct World;
struct TransportService;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct Actor;
struct TESBoundObject;
struct ExtraDataList;
struct NiPoint3;
class WorldItemService
{
public:
    WorldItemService(World&, entt::dispatcher&, TransportService&) noexcept;
    static bool Drop(Actor*, TESBoundObject*, ExtraDataList*, int32_t, const NiPoint3*) noexcept;
    static bool Pickup(Actor*, TESObjectREFR*, int32_t) noexcept;
    bool PhysicsIdentity(uint32_t aHandle, uint64_t& aId, uint64_t& aEpoch) const noexcept;

private:
    struct Slot
    {
        WorldItemRegistry::Mapping Map;
        WorldItemState State;
        uint64_t Capture{}, Apply{}, Deadline{}, NextCapture{}, NextApply{}, Received{}, InputAt{}, LastInputTick{};
        bool CaptureInteraction{}, ApplyInput{};
        std::optional<WorldItemMotion> Input;
    };
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnSnapshot(const NotifyWorldItems&) noexcept;
    void OnTransfer(const NotifyWorldItemTransfer&) noexcept;
    void OnMotion(const NotifyWorldItemMotion&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    bool Start(RequestWorldItemTransfer&) noexcept;
    bool Barrier() noexcept;
    bool Count(GameId, int64_t&) noexcept;
    bool Reconcile() noexcept;
    bool Receive() noexcept;
    void Physics(uint64_t) noexcept;
    void Fail(const char*) noexcept;
    static bool ValidatePhysics(uint32_t, uint64_t) noexcept;
    uint64_t Cookie() noexcept;
    World& m_world;
    TransportService& m_transport;
    std::array<Slot, WorldItemWire::kSlots> m_slots;
    std::optional<RequestWorldItemTransfer> m_intent;
    std::optional<NotifyWorldItems> m_snapshot, m_inbox;
    CharacterIdentity m_session, m_character;
    WorldItemTimeline m_timeline;
    uint64_t m_generation{}, m_loadGeneration{}, m_cursor{}, m_queryNonce{}, m_operation{}, m_cookie{}, m_sequence{};
    uint64_t m_hostEpoch{}, m_barrierGeneration{}, m_deadline{}, m_nextQuery{}, m_retry{}, m_waiting{};
    uint32_t m_scan{}, m_reconcile{}, m_physicsCursor{};
    bool m_ready{}, m_barrier{}, m_mutated{}, m_received{}, m_caughtUp{};
    entt::scoped_connection m_connect, m_disconnect, m_update, m_snapshotConnection, m_transferConnection, m_motionConnection;
    static WorldItemService* s_instance;
};
