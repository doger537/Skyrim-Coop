#pragma once
#include <Events/PacketEvent.h>
#include <CoopSleepVote.h>

struct World;
struct UpdateEvent;
struct RequestCoopSleep;

class CoopSleepService
{
public:
    CoopSleepService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
private:
    void OnRequest(const PacketEvent<RequestCoopSleep>& acPacket) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    bool Refresh(uint32_t aPartyId, CoopSleepVote& aVote) noexcept;
    void Broadcast(const CoopSleepVote& acVote) noexcept;
    World& m_world;
    Map<uint32_t, CoopSleepVote> m_votes;
    uint64_t m_lastRound{};
    entt::scoped_connection m_requestConnection;
    entt::scoped_connection m_updateConnection;
};
