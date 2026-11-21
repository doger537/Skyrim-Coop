#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <Messages/RequestQuestUpdate.h>
#include <QuestRunParticipants.h>

struct World;
struct RequestQuestUpdate;
struct Player;

/**
 * @brief Dispatch quest sync messages.
 *
 * Novel shared changes come from the session host; peer echoes are idempotent.
 */
class QuestService
{
public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);
    bool AllowsMisc() const noexcept;
    void FillPlayerJournal(Player& aPlayer) const noexcept;

private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;
    bool Commit(Player& aInitiator, const RequestQuestUpdate& acChange, const Campaign::QuestRunParticipants* apParticipants) noexcept;
    World& m_world;

    entt::scoped_connection m_questUpdateConnection;
};
