#pragma once
#include <Events/PacketEvent.h>
#include <Messages/RequestWorldCheckpoint.h>
#include <Messages/NotifyWorldCheckpoint.h>

struct World;
struct Player;
struct UpdateEvent;

class WorldCheckpointService
{
public:
    WorldCheckpointService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    bool IsFrozen() const noexcept;
    bool CanAuthenticate(const CharacterIdentity& acCharacter, uint64_t aToken, const CheckpointAdmissionProof& acProof, String& aError) noexcept;

private:
    enum class Step { Idle, PrepareSave, Saving, PrepareLoad, WaitingReconnect };
    bool Initialize() noexcept;
    bool ReadManifest(uint64_t aId, WorldCheckpoint& aManifest) noexcept;
    void OnRequest(const PacketEvent<RequestWorldCheckpoint>& acPacket) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void Begin(Player& aLeader, uint64_t aLoadId) noexcept;
    void Cancel(NotifyWorldCheckpoint::Error aError) noexcept;
    void Send(NotifyWorldCheckpoint::Phase aPhase) noexcept;
    void Restore() noexcept;
    std::array<Player*, 2> Players() noexcept;

    World& m_world;
    Step m_step{Step::Idle};
    bool m_initialized{};
    NotifyWorldCheckpoint m_control;
    uint8_t m_prepared{};
    uint8_t m_saved{};
    uint64_t m_deadline{};
    entt::scoped_connection m_requestConnection;
    entt::scoped_connection m_updateConnection;
};
