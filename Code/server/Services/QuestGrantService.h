#pragma once
#include <Events/PacketEvent.h>
#include <QuestGrant.h>
struct World;
struct RequestQuestGrants;
struct RequestSharedQuestEffect;
struct UpdateEvent;
class QuestGrantService
{
public:
    QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    bool HasPending() const noexcept { return !m_effects.empty(); }
private:
    void OnQuery(const PacketEvent<RequestQuestGrants>& acPacket) noexcept;
    void OnEffect(const PacketEvent<RequestSharedQuestEffect>& acPacket) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    struct Effect
    {
        Campaign::QuestGrantPlan Plan;
        std::array<CharacterIdentity, 2> Members;
        std::array<uint32_t, 2> ParticipantIds{};
        std::array<uint64_t, 2> Admissions{};
        std::array<bool, 2> Applied{};
        uint32_t Host{};
        uint64_t Nonce{}, Admission{}, WorldEpoch{}, Deadline{}, Revision{};
    };
    Vector<Effect> m_effects;
    World& m_world;
    entt::scoped_connection m_queryConnection;
    entt::scoped_connection m_effectConnection, m_updateConnection;
};
