#pragma once
#include <Messages/PlacedPhysicsMessages.h>
#include <Forms/PhysicalCellScan.h>
#include <Havok/PhysicsReplication.h>
#include <deque>
#include <optional>
struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
class PlacedPhysicsService
{
public:
    PlacedPhysicsService(World&, entt::dispatcher&, TransportService&) noexcept;
    const Vector<GameId>& LoadedCells() const noexcept { return m_cells; }
    Vector<GameId> SnapshotLoadedCells() noexcept;
    bool Authority(GameId aId, uint64_t& aEpoch) const noexcept;
    const PlacedPhysicsState* Find(GameId aId) const noexcept;
    bool HasPendingOwnership() const noexcept;

private:
    enum class Work : uint8_t
    {
        Probe,
        Owner,
        Held,
        Remote,
        Seed,
        Input
    };
    struct Entry
    {
        PlacedPhysicsState State, Previous;
        uint32_t Handle{};
        uint64_t Next{}, Received{}, Retry{}, LastInput{};
        bool Known{}, Seed{};
        std::optional<PlacedPhysicsState> Input;
    };
    struct Job
    {
        uint64_t Cookie{}, Epoch{}, Deadline{};
        GameId Id;
        uint32_t Handle{};
        Work Mode{};
    };
    bool Queue(GameId, uint32_t, Work, const RigidBodyRig* = nullptr) noexcept;
    void Scope(uint64_t, bool aReport = true) noexcept;
    void Discover() noexcept;
    void Poll(uint64_t) noexcept;
    void OnPose(const NotifyPlacedPhysics&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    static bool Validate(uint32_t, uint64_t) noexcept;
    World& m_world;
    TransportService& m_transport;
    Map<GameId, Entry> m_entries;
    std::array<Job, 8> m_jobs{};
    Vector<GameId> m_cells;
    std::deque<uint32_t> m_candidates;
    PhysicalCellScan::Cursor m_scan;
    uint64_t m_generation{}, m_cookie{}, m_sequence{}, m_nextScope{};
    size_t m_cellCursor{};
    GameId m_entryCursor;
    entt::scoped_connection m_pose, m_update, m_disconnect;
    static PlacedPhysicsService* s_instance;
};
