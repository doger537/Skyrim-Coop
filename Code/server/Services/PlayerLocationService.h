#pragma once
struct World;
struct UpdateEvent;

struct PlayerLocationService
{
    PlayerLocationService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
private:
    void OnUpdate(const UpdateEvent&) noexcept;
    World& m_world;
    uint64_t m_sequence{};
    uint64_t m_nextUpdate{};
    entt::scoped_connection m_updateConnection;
};
