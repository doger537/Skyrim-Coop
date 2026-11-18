#pragma once
#include <QuestGrant.h>
#include <Messages/NotifyQuestGrant.h>
#include <Messages/RequestQuestGrants.h>
#include <optional>

struct World;
struct TransportService;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;
class QuestGrantService
{
public:
    QuestGrantService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    // Called only by an admitted native grant broker after the original effect
    // has completed in THIS runtime. Never persist this marker in a sidecar.
    bool RecordNativeExecution(GameId aQuest, uint32_t aRun, const String& acEvent) noexcept;
private:
    struct Executed { GameId Quest; uint32_t Run{}; String Event; };
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnReceipt(const NotifyQuestGrant& acMessage) noexcept;
    void Apply(const NotifyQuestGrant& acMessage) noexcept;
    void Fail(const char* apReason) noexcept;
    bool Preflight(const Campaign::QuestGrantReceipt& acReceipt) noexcept;
    World& m_world;
    TransportService& m_transport;
    CharacterIdentity m_character;
    String m_worldId;
    uint64_t m_generation{}, m_nonce{}, m_nextPoll{}, m_deadline{}, m_cursor{}, m_nativeGeneration{}, m_nativeLoadGeneration{};
    bool m_ready{}, m_available{}, m_waiting{}, m_mutated{}, m_pending{}, m_skipNative{};
    RequestQuestGrants m_query;
    std::optional<Campaign::QuestGrantReceipt> m_receipt;
    Inventory m_delivery;
    size_t m_item{};
    Vector<Executed> m_executed;
    entt::scoped_connection m_connectedConnection, m_disconnectedConnection, m_updateConnection, m_receiptConnection;
};
