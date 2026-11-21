#include <Services/CoopSleepService.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Events/CoopSleepIntentEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/RequestCoopSleep.h>
#include <Messages/NotifyCoopSleep.h>
#include <Messages/ServerTimeSettings.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <limits>

CoopSleepService::CoopSleepService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_intentConnection = aDispatcher.sink<CoopSleepIntentEvent>().connect<&CoopSleepService::OnIntent>(this);
    m_notifyConnection = aDispatcher.sink<NotifyCoopSleep>().connect<&CoopSleepService::OnNotify>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CoopSleepService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CoopSleepService::OnDisconnected>(this);
}

void CoopSleepService::Reset() noexcept
{
    m_round = 0;
    m_preparedRound = 0;
    m_menuSession = 0;
    m_deadline = 0;
    m_hours = 0;
}

void CoopSleepService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    Reset();
    m_sequence = 0;
    m_committedRound = 0;
    m_lastNoticeRound = 0;
}

void CoopSleepService::Send(CoopSleepVote::Action aAction, uint8_t aHours) noexcept
{
    if (!m_transport.IsConnected() || m_sequence == std::numeric_limits<uint64_t>::max())
        return;
    RequestCoopSleep request;
    request.Action = aAction;
    request.Sequence = ++m_sequence;
    request.Round = m_round;
    request.Tick = m_world.GetTick();
    request.Hours = aHours;
    m_transport.Send(request);
}

void CoopSleepService::OnIntent(const CoopSleepIntentEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected() || acEvent.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        !SleepWaitMenu::IsPending(acEvent.MenuSession, acEvent.Sleeping) || (m_round != 0 && m_preparedRound == m_round))
        return;
    const auto& party = m_world.GetPartyService();
    if (!party.IsInParty() || party.GetPartyMembers().size() != 2)
    {
        m_world.GetOverlayService().SendSystemMessage("Для совместного сна создайте группу из двух игроков.");
        return;
    }
    m_menuSession = acEvent.MenuSession;
    m_sleeping = acEvent.Sleeping;
    m_hours = acEvent.Hours;
    m_deadline = m_world.GetTick() + CoopSleepVote::kWindowMs;
    Send(CoopSleepVote::Action::Vote, m_hours);
}

void CoopSleepService::OnNotify(const NotifyCoopSleep& acMessage) noexcept
{
    const uint32_t localId = m_transport.GetLocalPlayerId();
    if (!acMessage.IsValid() || acMessage.Round <= m_committedRound ||
        (acMessage.Members[0] != localId && acMessage.Members[1] != localId))
        return;
    const auto& party = m_world.GetPartyService();
    if (!party.IsInParty() || party.GetLeaderPlayerId() != acMessage.Members[0])
        return;
    m_round = acMessage.Round;
    m_deadline = acMessage.Deadline;
    if (acMessage.Phase == CoopSleepVote::Phase::Waiting)
    {
        const uint8_t localVote = acMessage.Members[0] == localId ? 1 : 2;
        if ((acMessage.Votes & localVote) && m_hours == 0)
        {
            Send(CoopSleepVote::Action::Cancel);
            Reset();
            return;
        }
        if (m_lastNoticeRound != m_round)
        {
            m_lastNoticeRound = m_round;
            m_world.GetOverlayService().ShowCoopRest(static_cast<uint8_t>(acMessage.Phase), 0, m_hours != 0);
            m_world.GetOverlayService().SendSystemMessage("Совместный отдых: оба игрока должны выбрать сон или ожидание в течение 30 секунд.");
        }
        return;
    }
    if (acMessage.Phase == CoopSleepVote::Phase::Cancelled)
    {
        if (m_hours != 0)
        {
            m_world.GetOverlayService().ShowCoopRest(static_cast<uint8_t>(acMessage.Phase), 0, true);
            m_world.GetOverlayService().SendSystemMessage("Совместный отдых отменён. Время не проматывается.");
        }
        Reset();
        return;
    }
    if (acMessage.Phase == CoopSleepVote::Phase::Preparing && m_preparedRound != m_round)
    {
        if (m_hours == 0 || !SleepWaitMenu::IsPending(m_menuSession, m_sleeping))
        {
            Send(CoopSleepVote::Action::Cancel);
            Reset();
            return;
        }
        m_preparedRound = m_round;
        Send(CoopSleepVote::Action::Ready);
        return;
    }
    if (acMessage.Phase == CoopSleepVote::Phase::Committed && acMessage.Round > m_committedRound)
    {
        m_committedRound = acMessage.Round;
        m_world.GetOverlayService().ShowCoopRest(static_cast<uint8_t>(acMessage.Phase), acMessage.Hours, true);
        ServerTimeSettings time;
        time.timeModel = acMessage.Time;
        m_world.GetDispatcher().trigger(time);
        SleepWaitMenu::BeginApproved(m_menuSession, acMessage.Hours, m_sleeping);
        Reset();
    }
}

void CoopSleepService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_hours == 0)
        return;
    if (!m_world.GetPartyService().IsInParty() || m_world.GetPartyService().GetPartyMembers().size() != 2 ||
        !SleepWaitMenu::IsPending(m_menuSession, m_sleeping) || m_world.GetTick() >= m_deadline)
    {
        if (m_round != 0)
            Send(CoopSleepVote::Action::Cancel);
        Reset();
    }
}
