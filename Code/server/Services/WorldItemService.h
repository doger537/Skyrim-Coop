#pragma once
#include <Events/PacketEvent.h>
#include <Messages/WorldItemMessages.h>
#include <Structs/PhysicsFrame.h>
struct World;
class WorldItemService
{
public:
    WorldItemService(World&, entt::dispatcher&) noexcept;
    bool HasPending() const noexcept;
    bool Flush() noexcept;
    const WorldItemState* Find(uint64_t) const noexcept;
    bool CommitPhysics(const PhysicsFrame&) noexcept;

private:
    struct Client
    {
        uint32_t Id{};
        uint64_t Admission{}, NextQuery{}, Awaiting{};
        RequestWorldItemTransfer Last;
        NotifyWorldItemTransfer Reply;
    };
    struct Item
    {
        WorldItemState State;
        uint64_t Received{}, Interacted{}, InputSequence{};
        uint32_t Interactor{};
        bool Dirty{};
    };
    Client& GetClient(Player&) noexcept;
    bool Ready(Player&, uint64_t& aHostEpoch) noexcept;
    bool Load(uint64_t aHostEpoch) noexcept;
    void Snapshot(NotifyWorldItems&) noexcept;
    void Broadcast() noexcept;
    void OnQuery(const PacketEvent<RequestWorldItems>&) noexcept;
    void OnTransfer(const PacketEvent<RequestWorldItemTransfer>&) noexcept;
    void OnMotion(const PacketEvent<RequestWorldItemMotion>&) noexcept;
    World& m_world;
    Vector<Item> m_items;
    Vector<Client> m_clients;
    uint64_t m_epoch{}, m_hostEpoch{}, m_leaderAdmission{};
    entt::scoped_connection m_queryConnection, m_transferConnection, m_motionConnection;
};
