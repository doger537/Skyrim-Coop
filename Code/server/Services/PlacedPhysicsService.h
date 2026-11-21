#pragma once
#include <Events/PacketEvent.h>
#include <Messages/PlacedPhysicsMessages.h>
#include <Structs/PhysicsFrame.h>
struct World;
struct UpdateEvent;
class PlacedPhysicsService
{
public:
    PlacedPhysicsService(World&, entt::dispatcher&) noexcept;
    bool Flush() noexcept;
    bool CommitPhysics(const PhysicsFrame&) noexcept;
    bool Authority(GameId, uint32_t, uint64_t) const noexcept;
    const PlacedPhysicsState* Find(GameId) const noexcept;

private:
    struct Record
    {
        PlacedPhysicsState State;
        uint64_t Received{}, InteractionAt{}, InputSequence{};
        uint32_t Interactor{};
        bool Dirty{};
    };
    struct Scope
    {
        uint32_t Player{};
        uint64_t Admission{}, At{};
        Vector<GameId> Cells;
        size_t Seed{};
    };
    bool Initialize() noexcept;
    bool Covers(uint32_t, GameId) const noexcept;
    Player* Choose(GameId) noexcept;
    void Publish(const Record&, bool aReliable) noexcept;
    void Reassign(Record&, Player*) noexcept;
    void OnPacket(const PacketEvent<RequestPlacedPhysics>&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    World& m_world;
    Vector<Record> m_records;
    Vector<Scope> m_scopes;
    uint64_t m_epoch{}, m_nextEpoch{};
    size_t m_cursor{};
    entt::scoped_connection m_packet, m_update;
};
