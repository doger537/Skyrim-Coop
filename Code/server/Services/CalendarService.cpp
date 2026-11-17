#include <Services/CalendarService.h>
#include <Services/WorldCheckpointService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/ServerTimeSettings.h>

#include "Game/Player.h"
#include <algorithm>
#include <cmath>

bool CalendarService::AdvanceHours(uint8_t aHours) noexcept
{
    if (aHours < 1 || aHours > 24 || !m_dateTime.AdvanceHours(aHours))
        return false;
    m_tickBaseline.Rebase(GameServer::Get()->GetTick());
    SendTimeResync();
    return true;
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::OnUpdate>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&CalendarService::OnPlayerJoin>(this);
}

void CalendarService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t delta = m_tickBaseline.Sample(GameServer::Get()->GetTick());
    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen())
        return;
    m_dateTime.Update(delta);
    if (m_resyncCadence.Advance(delta))
        SendTimeResync();
}

void CalendarService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    ServerTimeSettings timeMsg;
    // Only the initial world owner seeds the clock. A later guest's save must
    // never move the host's campaign into another day or year.
    const bool initialized = !m_timeSetFromFirstPlayer && acEvent.PlayerTime.m_timeModel.IsValid();
    if (initialized)
    {
        const float scale = m_dateTime.m_timeModel.TimeScale;
        m_dateTime = acEvent.PlayerTime;
        m_dateTime.m_timeModel.TimeScale = scale;
        m_timeSetFromFirstPlayer = true;
        m_tickBaseline.Rebase(GameServer::Get()->GetTick());
    }
    timeMsg.timeModel = m_dateTime.m_timeModel;

    if (initialized)
        GameServer::Get()->SendToPlayers(timeMsg);
    else
        acEvent.pPlayer->Send(timeMsg);
}

bool CalendarService::SetTime(int aHours, int aMinutes, float aScale) noexcept
{
    if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59 && std::isfinite(aScale) && aScale >= 0.f && aScale <= 1000.f)
    {
        m_dateTime.m_timeModel.TimeScale = aScale;
        // encode time as skyrim time
        const float hour = static_cast<float>(aHours) + static_cast<float>(aMinutes) / 60.f;
        m_dateTime.m_timeModel.DaysPassed = std::max(0.0, m_dateTime.m_timeModel.DaysPassed + (hour - m_dateTime.m_timeModel.Time) / 24.0);
        m_dateTime.m_timeModel.Time = hour;

        SendTimeResync();

        GameServer::Get()->GetWorld().GetScriptService().HandleSetTime(aHours, aMinutes, aScale);
        return true;
    }
    return false;
}

bool CalendarService::SetDate(int aDay, int aMonth, float aYear) noexcept
{
    if (aMonth >= 0 && aMonth < 12 && aYear >= 0 && aYear <= 999)
    {
        auto maxDays = m_dateTime.GetNumberOfDaysByMonthIndex(aMonth);
        if (aDay >= 1 && aDay <= maxDays)
        {
            m_dateTime.m_timeModel.Day = aDay;
            m_dateTime.m_timeModel.Month = aMonth;
            m_dateTime.m_timeModel.Year = aYear;
            SendTimeResync();
            return true;
        }
    }
    return false;
}

void CalendarService::SendTimeResync() noexcept
{
    ServerTimeSettings timeMsg;
    timeMsg.timeModel = m_dateTime.m_timeModel;
    GameServer::Get()->SendToLoaded(timeMsg);
}

CalendarService::TTime CalendarService::GetTime() const noexcept
{
    const auto hour = floor(m_dateTime.m_timeModel.Time);
    const int flatMinutes = std::clamp(static_cast<int>(std::floor((m_dateTime.m_timeModel.Time - hour) * 60.f + 0.0001f)), 0, 59);
    return {static_cast<int>(hour), flatMinutes};
}

CalendarService::TTime CalendarService::GetRealTime() noexcept
{
    const auto t = std::time(nullptr);
    int h = (t / 3600) % 24;
    int m = (t / 60) % 60;
    return {h, m};
}

CalendarService::TDate CalendarService::GetDate() const noexcept
{
    return {m_dateTime.m_timeModel.Day, m_dateTime.m_timeModel.Month, m_dateTime.m_timeModel.Year};
}

bool CalendarService::SetTimeScale(float aScale) noexcept
{
    if (aScale >= 0.f && aScale <= 1000.f)
    {
        m_dateTime.m_timeModel.TimeScale = aScale;

        ServerTimeSettings timeMsg;
        timeMsg.timeModel = m_dateTime.m_timeModel;
        GameServer::Get()->SendToPlayers(timeMsg);
        return true;
    }

    return false;
}

void CalendarService::RestoreCheckpointModel(const TimeModel& acModel) noexcept
{
    if (!acModel.IsValid())
        return;
    m_dateTime = DateTime(acModel);
    m_timeSetFromFirstPlayer = true;
    m_tickBaseline.Rebase(GameServer::Get()->GetTick());
}
