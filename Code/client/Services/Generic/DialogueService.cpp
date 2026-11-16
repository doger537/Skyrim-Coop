#include <Services/DialogueService.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Components.h>
#include <Utils.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Messages/RequestDialogueLease.h>
#include <Messages/NotifyDialogueLease.h>
#include <Games/Misc/MenuTopicManager.h>
#include <ScopedReference.h>
#include <Actor.h>
#include <AI/DialogueMovement.h>
#include <limits>

DialogueService::DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DialogueService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyDialogueLease>().connect<&DialogueService::OnNotify>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&DialogueService::OnDisconnected>(this);
}

void DialogueService::ClearHolds() noexcept
{
    DialogueMovement::Reset();
    m_holds.clear();
}

void DialogueService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    ClearHolds();
    m_actor = UINT32_MAX;
    m_ownershipEpoch = 0;
    m_sequence = m_lastRevision = m_nextUpdate = m_nextRefresh = 0;
}

void DialogueService::Send(bool aActive) noexcept
{
    if (m_actor == UINT32_MAX || !m_transport.IsConnected() || m_sequence == std::numeric_limits<uint64_t>::max())
        return;
    RequestDialogueLease request;
    request.ServerId = m_actor;
    request.OwnershipEpoch = m_ownershipEpoch;
    request.Sequence = ++m_sequence;
    request.Tick = m_world.GetTick();
    request.Active = aActive;
    m_transport.Send(request);
}

void DialogueService::OnNotify(const NotifyDialogueLease& acMessage) noexcept
{
    if (!m_transport.IsConnected() || !acMessage.IsValid() || acMessage.Revision <= m_lastRevision)
        return;
    // This single server sends these reliable notifications in revision order.
    m_lastRevision = acMessage.Revision;
    if (auto it = m_holds.find(acMessage.ServerId); it != m_holds.end())
    {
        DialogueMovement::Release(it->second.FormId);
        m_holds.erase(it);
    }
    const uint64_t now = m_world.GetTick();
    if (!acMessage.Active || acMessage.SpeakerPlayerId == m_transport.GetLocalPlayerId() ||
        acMessage.Deadline <= now || acMessage.Deadline - now > RequestDialogueLease::kDurationMs + 2000)
        return;
    const auto entity = Utils::FindEntityByServerId(acMessage.ServerId);
    const auto* local = entity ? m_world.try_get<LocalComponent>(*entity) : nullptr;
    const auto* form = entity ? m_world.try_get<FormIdComponent>(*entity) : nullptr;
    if (!local || !form || local->OwnershipEpoch != acMessage.OwnershipEpoch || m_holds.size() >= 8)
        return;
    const uint32_t duration = static_cast<uint32_t>(std::min(acMessage.Deadline - now, RequestDialogueLease::kDurationMs));
    if (DialogueMovement::Hold(form->Id, duration))
        m_holds.emplace(acMessage.ServerId, HeldActor{form->Id, acMessage.OwnershipEpoch, now + duration});
}

void DialogueService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected() || !DialogueMovement::IsSupported())
        return;
    const uint64_t now = m_world.GetTick();
    if (now < m_nextUpdate)
        return;
    m_nextUpdate = now + 100;
    for (auto it = m_holds.begin(); it != m_holds.end();)
    {
        const auto entity = Utils::FindEntityByServerId(it->first);
        const auto* local = entity ? m_world.try_get<LocalComponent>(*entity) : nullptr;
        const auto* form = entity ? m_world.try_get<FormIdComponent>(*entity) : nullptr;
        if (it->second.Deadline <= now || !local || !form || form->Id != it->second.FormId ||
            local->OwnershipEpoch != it->second.OwnershipEpoch)
        {
            DialogueMovement::Release(it->second.FormId);
            it = m_holds.erase(it);
        }
        else
            ++it;
    }
    uint32_t actor = UINT32_MAX;
    uint32_t ownershipEpoch{};
    const auto* manager = MenuTopicManager::Get();
    if (manager && manager->menuOpen && m_world.GetPartyService().IsInParty() && m_world.GetPartyService().GetPartyMembers().size() == 2)
    {
        ScopedReference reference(manager->speaker.handle.iBits);
        const auto* speaker = Cast<Actor>(reference.Get());
        if (speaker && !speaker->IsDead())
        {
            const auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
            for (const auto entity : view)
                if (view.get<FormIdComponent>(entity).Id == speaker->formID)
                {
                    if (const auto* local = m_world.try_get<LocalComponent>(entity))
                    {
                        actor = local->Id;
                        ownershipEpoch = local->OwnershipEpoch;
                    }
                    else if (const auto* remote = m_world.try_get<RemoteComponent>(entity))
                    {
                        actor = remote->Id;
                        ownershipEpoch = remote->OwnershipEpoch;
                    }
                    break;
                }
        }
    }
    if (actor != m_actor || ownershipEpoch != m_ownershipEpoch)
    {
        Send(false);
        m_actor = actor;
        m_ownershipEpoch = ownershipEpoch;
        m_nextRefresh = 0;
    }
    if (m_actor != UINT32_MAX && now >= m_nextRefresh)
    {
        Send(true);
        m_nextRefresh = now + 750;
    }
}
