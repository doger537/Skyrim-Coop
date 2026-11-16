#pragma once
#include <Events/PacketEvent.h>

struct World;
struct Player;
struct UpdateEvent;
struct RequestDialogueLease;
struct DialogueLeaseComponent;

struct DialogueService
{
    DialogueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
private:
    struct SpeakerState
    {
        uint64_t Sequence{};
        uint64_t LastRefresh{};
    };
    void OnRequest(const PacketEvent<RequestDialogueLease>& acPacket) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    Player* FindPlayer(uint32_t aId) const noexcept;
    bool CanTalk(Player* apSpeaker, entt::entity aActor) const noexcept;
    void Broadcast(entt::entity aActor, const DialogueLeaseComponent& acLease, bool aActive) noexcept;
    void Release(entt::entity aActor) noexcept;
    World& m_world;
    Map<uint32_t, SpeakerState> m_speakers;
    uint64_t m_revision{};
    uint64_t m_nextUpdate{};
    entt::scoped_connection m_requestConnection;
    entt::scoped_connection m_updateConnection;
};
