#pragma once
#include <common/PlayerMarkerIds.h>
#include <array>
#include <optional>
#include <Messages/NotifyPartyLocations.h>
struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct ActorRemovedEvent;
struct PartyLeftEvent;

struct PlayerMarkerService
{
    PlayerMarkerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
private:
    struct Slot
    {
        std::optional<uint32_t> Player;
        uint32_t FormId{};
        uint32_t AnchorHandle{};
        GameId Cell{};
        GameId WorldSpace{};
    };
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept;
    void OnPlayerRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnLocations(const NotifyPartyLocations& acMessage) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;
    bool ClearAll() noexcept;
    void ClearSlot(uint32_t aSlot) noexcept;
    World& m_world;
    TransportService& m_transport;
    std::array<Slot, PlayerMarkers::kSlotCount> m_slots{};
    uint64_t m_nextUpdate{};
    bool m_needsCleanup{true};
    NotifyPartyLocations m_locations;
    uint64_t m_lastSequence{};
    uint64_t m_receivedAt{};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_actorRemovedConnection;
    entt::scoped_connection m_playerRemovedConnection;
    entt::scoped_connection m_locationsConnection;
    entt::scoped_connection m_partyLeftConnection;
};
