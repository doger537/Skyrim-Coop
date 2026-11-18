#pragma once
#include <Events/PacketEvent.h>
struct World;
struct RequestQuestGrants;
class QuestGrantService
{
public:
    QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
private:
    void OnQuery(const PacketEvent<RequestQuestGrants>& acPacket) noexcept;
    World& m_world;
    entt::scoped_connection m_queryConnection;
};
