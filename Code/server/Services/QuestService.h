#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <Messages/RequestQuestUpdate.h>
#include <QuestRunParticipants.h>
#include <deque>

struct World;
struct UpdateEvent;
struct RequestQuestUpdate;
struct Player;
struct RequestQuestDecision;

/**
 * @brief Dispatch quest sync messages.
 *
 * Guest changes are proposed to the current pair host before journal commit.
 */
class QuestService
{
public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);
    bool AllowsMisc() const noexcept;
    void FillPlayerJournal(Player& aPlayer) const noexcept;
    bool HasPendingAuthority() const noexcept { return !m_proposals.empty(); }

private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;
    void OnDecision(const PacketEvent<RequestQuestDecision>& acPacket) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    bool Commit(Player& aInitiator, const RequestQuestUpdate& acChange, const Campaign::QuestRunParticipants* apParticipants) noexcept;
    Player* FindPlayer(uint32_t aId) const noexcept;
    struct Proposal
    {
        uint64_t Id{};
        uint64_t WorldEpoch{};
        uint64_t Deadline{};
        uint32_t InitiatorId{};
        uint32_t HostId{};
        CharacterIdentity HostCharacter;
        Campaign::QuestRunParticipants Participants;
        RequestQuestUpdate Change;
        Vector<RequestQuestUpdate> DeferredHostChanges;
        bool Sent{};
    };
    bool ValidPair(const Proposal& acProposal) const noexcept;
    void FinishProposal(bool aAccepted) noexcept;
    std::deque<Proposal> m_proposals;
    uint64_t m_nextProposal{};

    World& m_world;

    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
    entt::scoped_connection m_decisionConnection;
};
