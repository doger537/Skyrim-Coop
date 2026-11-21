#pragma once
struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyRagdollPose;
class RagdollService
{
public:
    RagdollService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
private:
    void OnPose(const NotifyRagdollPose& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Poll() noexcept;
    void Capture(uint64_t aNow) noexcept;
    void Apply(uint64_t aNow) noexcept;
    void CaptureHeld(uint64_t aNow) noexcept;
    void ApplyInputs(uint64_t aNow) noexcept;
    uint64_t Cookie() noexcept;
    World& m_world;
    TransportService& m_transport;
    uint64_t m_cookie{}, m_sequence{}, m_generation{};
    entt::entity m_captureCursor{entt::null}, m_applyCursor{entt::null}, m_inputCursor{entt::null};
    entt::scoped_connection m_poseConnection, m_updateConnection, m_disconnectConnection;
};
