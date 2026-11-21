#pragma once
#include <Messages/PhysicsFrameMessages.h>
#include <Systems/PhysicsSaveMap.h>
#include <Forms/PhysicalCellScan.h>
#include <Havok/PhysicsReplication.h>
#include <future>
#include <deque>
struct World;
struct TransportService;
class PhysicsCheckpointService
{
public:
    PhysicsCheckpointService(World&, entt::dispatcher&, TransportService&) noexcept;
    bool Capture(uint64_t, uint64_t, size_t, const String&, const CharacterIdentity&, const std::filesystem::path&) noexcept;
    bool Load(uint64_t, uint64_t, size_t, const String&, const CharacterIdentity&, const std::filesystem::path&, const PhysicsFrame::Digest&) noexcept;
    void Tick() noexcept;
    void Cancel() noexcept;
    bool Ready(const PhysicsFrame::Digest&) const noexcept;
    bool MappingReady() const noexcept { return m_load && m_phase == Phase::Await; }

private:
    enum class Phase : uint8_t
    {
        Idle,
        Capture,
        Send,
        Await,
        Apply,
        Verify,
        Finish,
        Ready,
        ReadMap
    };
    struct Target
    {
        PhysicsFrameEntry State;
        uint32_t Form{}, Handle{};
        uint64_t Cookie{};
        bool Applied{}, Verified{}, Unknown{};
    };
    struct IOResult
    {
        bool Valid{};
        uint32_t Count{};
        PhysicsFrame::Digest Digest{};
        PhysicsSaveMap::Document Map;
    };
    struct Active
    {
        uint64_t Cookie{};
        size_t Target{};
    };
    bool Begin(uint64_t, uint64_t, size_t, const String&, const CharacterIdentity&, const std::filesystem::path&) noexcept;
    void Scan() noexcept;
    bool Describe(uint32_t, Target&) noexcept;
    void Poll() noexcept;
    void Apply() noexcept;
    void FinishAsync() noexcept;
    void OnFrame(const NotifyPhysicsFrame&) noexcept;
    void Fail(const char*) noexcept;
    static bool Validate(uint32_t, uint64_t) noexcept;
    World& m_world;
    TransportService& m_transport;
    Phase m_phase{Phase::Idle};
    uint64_t m_id{}, m_nonce{}, m_cookie{}, m_generation{};
    size_t m_slot{}, m_cell{}, m_sent{}, m_applyCursor{}, m_remaining{};
    bool m_load{}, m_scanned{}, m_end{}, m_failed{};
    PhysicsFrame m_offer, m_canonical;
    PhysicsFrame::Digest m_expected{};
    PhysicsSaveMap::Document m_map;
    std::filesystem::path m_path;
    std::future<IOResult> m_io;
    Vector<GameId> m_cells;
    PhysicalCellScan::Cursor m_scan;
    Vector<Target> m_targets;
    std::array<Target, 4> m_probes{};
    std::array<Active, 4> m_jobs{};
    std::deque<uint32_t> m_candidates;
    Set<uint32_t> m_seen;
    Map<uint32_t, std::pair<uint32_t, uint32_t>> m_actors;
    entt::scoped_connection m_frame;
    static PhysicsCheckpointService* s_instance;
};
