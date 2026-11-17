#pragma once
#include <Messages/RequestWorldCheckpoint.h>
#include <Messages/NotifyWorldCheckpoint.h>
#include <future>
#include <memory>
#include <atomic>

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct WorldCheckpointIntent;

class WorldCheckpointService
{
public:
    WorldCheckpointService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    bool IsFrozen() const noexcept { return m_frozen; }
    bool AwaitingReconnect() const noexcept { return m_loading && m_loaded; }
    bool NativeLoadCompleted() noexcept;
    void UnexpectedGameplay() noexcept;
    void Tick(const UpdateEvent&) noexcept;

private:
    struct Inspection
    {
        CheckpointPlayer Player;
        uint64_t Nonce{};
        bool Load{};
        bool Valid{};
    };
    void OnIntent(const WorldCheckpointIntent& acEvent) noexcept;
    void OnNotify(const NotifyWorldCheckpoint& acMessage) noexcept;
    void Apply(const NotifyWorldCheckpoint& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Send(RequestWorldCheckpoint::Kind aKind) noexcept;
    void Inspect(bool aLoad) noexcept;
    void Finish(bool aSuccess) noexcept;
    void Notice(const char* apMessage) noexcept;

    World& m_world;
    TransportService& m_transport;
    NotifyWorldCheckpoint m_control;
    std::future<Inspection> m_inspection;
    std::shared_ptr<std::atomic<bool>> m_cancelInspection;
    bool m_dirty{};
    size_t m_slot{};
    uint64_t m_deadline{};
    uint64_t m_previousToken{};
    uint64_t m_nextReconnect{};
    bool m_active{};
    bool m_frozen{};
    bool m_openedPause{};
    bool m_prepared{};
    bool m_loading{};
    bool m_loaded{};
    bool m_inspected{};
    bool m_wroteToken{};
    entt::scoped_connection m_intentConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
};
