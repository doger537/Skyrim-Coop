#pragma once
#include <common/CoopSleepVote.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct CoopSleepIntentEvent;
struct NotifyCoopSleep;

class CoopSleepService
{
public:
    CoopSleepService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
private:
    void OnIntent(const CoopSleepIntentEvent& acEvent) noexcept;
    void OnNotify(const NotifyCoopSleep& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Send(CoopSleepVote::Action aAction, uint8_t aHours = 0) noexcept;
    void Reset() noexcept;
    World& m_world;
    TransportService& m_transport;
    uint64_t m_sequence{};
    uint64_t m_round{};
    uint64_t m_preparedRound{};
    uint64_t m_committedRound{};
    uint64_t m_menuSession{};
    uint64_t m_deadline{};
    uint64_t m_lastNoticeRound{};
    uint8_t m_hours{};
    bool m_sleeping{};
    entt::scoped_connection m_intentConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
};
