#include <Services/CalendarService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>

constexpr float kTransitionSpeed = 5.f;

bool CalendarService::s_gameClockLocked = false;

bool CalendarService::AllowGameTick() noexcept
{
    return !s_gameClockLocked;
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_transport(aTransport)
{
    m_timeUpdateConnection = aDispatcher.sink<ServerTimeSettings>().connect<&CalendarService::OnTimeUpdate>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::HandleUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CalendarService::OnDisconnected>(this);
}

void CalendarService::OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept
{
    if (!acMessage.timeModel.IsValid())
        return;
    // disable the game clock
    ToggleGameClock(false);
    m_tickBaseline.Rebase(m_world.GetTick());
    m_fadeTimer = 0.f;
    m_onlineTime = DateTime(acMessage.timeModel);
}

void CalendarService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // signal a time transition
    m_fadeTimer = 0.f;
    m_tickBaseline.Reset();
    ToggleGameClock(true);
}

float CalendarService::TimeInterpolate(const TimeModel& aFrom, TimeModel& aTo) const
{
    return ReplicationClock::BlendHour(aFrom.Time, aTo.Time, m_fadeTimer / kTransitionSpeed);
}

void CalendarService::ToggleGameClock(bool aEnable)
{
    auto* pGameTime = TimeData::Get();
    m_offlineTime.m_timeModel.Day = pGameTime->GameDay->f;
    m_offlineTime.m_timeModel.Month = pGameTime->GameMonth->f;
    m_offlineTime.m_timeModel.Year = pGameTime->GameYear->f;
    m_offlineTime.m_timeModel.Time = pGameTime->GameHour->f;
    m_offlineTime.m_timeModel.TimeScale = pGameTime->TimeScale->f;
    m_offlineTime.m_timeModel.DaysPassed = pGameTime->GameDaysPassed->f;

    s_gameClockLocked = !aEnable;
}

void CalendarService::HandleUpdate(const UpdateEvent& aEvent) noexcept
{
    if (s_gameClockLocked)
    {
        const auto updateDelta = static_cast<float>(aEvent.Delta);
        auto* pGameTime = TimeData::Get();

        const auto now = m_world.GetTick();
        const uint64_t delta = m_tickBaseline.Sample(now);

        m_onlineTime.Update(delta);
        pGameTime->TimeScale->f = m_onlineTime.m_timeModel.TimeScale;
        pGameTime->GameDay->f = m_onlineTime.m_timeModel.Day;
        pGameTime->GameMonth->f = m_onlineTime.m_timeModel.Month;
        pGameTime->GameYear->f = m_onlineTime.m_timeModel.Year;
        pGameTime->GameDaysPassed->f = static_cast<float>(m_onlineTime.m_timeModel.DaysPassed);

        // time transition in
        if (m_fadeTimer < kTransitionSpeed)
        {
            pGameTime->GameHour->f = TimeInterpolate(m_offlineTime.m_timeModel, m_onlineTime.m_timeModel);
            m_fadeTimer += updateDelta;
        }
        else
            pGameTime->GameHour->f = m_onlineTime.m_timeModel.Time;
    }
}
