#pragma once
struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyDialogueLease;

struct DialogueService
{
    DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
private:
    struct HeldActor
    {
        uint32_t FormId{};
        uint32_t OwnershipEpoch{};
        uint64_t Deadline{};
    };
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnNotify(const NotifyDialogueLease& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Send(bool aActive) noexcept;
    void ClearHolds() noexcept;
    World& m_world;
    TransportService& m_transport;
    Map<uint32_t, HeldActor> m_holds;
    uint32_t m_actor{UINT32_MAX};
    uint32_t m_ownershipEpoch{};
    uint64_t m_sequence{};
    uint64_t m_lastRevision{};
    uint64_t m_nextUpdate{};
    uint64_t m_nextRefresh{};
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_disconnectConnection;
};
