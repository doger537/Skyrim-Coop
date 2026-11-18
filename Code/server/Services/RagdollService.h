#pragma once
#include <Events/PacketEvent.h>
struct World;
struct RequestRagdollPose;
class RagdollService
{
public:
    RagdollService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
private:
    void OnPose(const PacketEvent<RequestRagdollPose>& acPacket) noexcept;
    World& m_world;
    entt::scoped_connection m_poseConnection;
};
