#pragma once
#include <NativeQuestGate.h>
#include <QuestGrant.h>
#include <Messages/RequestSharedQuestEffect.h>
struct World; struct TransportService; struct UpdateEvent; struct DisconnectedEvent; struct NotifySharedQuestEffect;
class SharedQuestEffectService
{
public:
    SharedQuestEffectService(World&, entt::dispatcher&, TransportService&) noexcept;
    bool Records(uint64_t aCookie) noexcept { return Find(aCookie) != nullptr; }
    bool GuestReady(const Campaign::QuestGrantPlan&) noexcept;
    void GuestApplied(const Campaign::QuestGrantPlan&) noexcept;
private:
    struct Pending
    {
        Campaign::QuestGrantPlan Plan;
        String Source;
        uint32_t Form{};
        uint64_t Cookie{}, Admission{}, WorldEpoch{}, Deadline{}, Retry{}, Revision{};
        int64_t Before{};
        bool Permitted{}, Executed{}, Committed{}, Delivered{}, Failed{}, Empty{}, Guest{};
    };
    NativeQuestGate::Decision Begin(const NativeQuestGate::Invocation&) noexcept;
    NativeQuestGate::Decision Poll(uint64_t) noexcept;
    void Executed(uint64_t) noexcept;
    void Failed(uint64_t) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnect(const DisconnectedEvent&) noexcept;
    void OnNotify(const NotifySharedQuestEffect&) noexcept;
    Pending* Find(uint64_t) noexcept;
    bool Count(uint32_t, int64_t&) noexcept;
    void Send(Pending&, uint8_t) noexcept;
    World& m_world; TransportService& m_transport;
    Vector<Pending> m_pending;
    entt::scoped_connection m_updateConnection, m_disconnectConnection, m_notifyConnection;
    static SharedQuestEffectService* s_instance;
};
