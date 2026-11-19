#pragma once
#include <Events/PacketEvent.h>
#include <Messages/PhysicsFrameMessages.h>
#include <Messages/NotifyWorldCheckpoint.h>
struct World;
struct UpdateEvent;
class PhysicsCheckpointService
{
public:
    PhysicsCheckpointService(World&, entt::dispatcher&) noexcept;
    bool Capture(const NotifyWorldCheckpoint&) noexcept;
    bool Load(const NotifyWorldCheckpoint&) noexcept;
    void Cancel() noexcept;
    bool Ready() const noexcept { return m_active && m_applied == 3 && !m_failed; }
    bool Failed() const noexcept { return m_failed; }
    const PhysicsFrame& Frame() const noexcept { return m_frame; }
    const PhysicsFrame::Digest& Digest() const noexcept { return m_digest; }

private:
    bool Merge() noexcept;
    void OnPacket(const PacketEvent<RequestPhysicsFrame>&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    World& m_world;
    NotifyWorldCheckpoint m_control;
    std::array<PhysicsFrame, 2> m_offers;
    std::array<size_t, 2> m_sent{};
    std::array<bool, 2> m_ended{};
    PhysicsFrame m_frame;
    PhysicsFrame::Digest m_digest{};
    bool m_active{}, m_canonical{}, m_failed{};
    uint8_t m_applied{};
    entt::scoped_connection m_packet, m_update;
};
