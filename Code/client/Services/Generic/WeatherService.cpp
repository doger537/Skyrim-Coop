#include <Services/WeatherService.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Messages/RequestWeatherChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>

#include <Sky/Sky.h>
#include <Forms/TESWeather.h>

namespace
{
constexpr double kWeatherResyncSeconds = 60.0;
}

WeatherService::WeatherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WeatherService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&WeatherService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&WeatherService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&WeatherService::OnPartyLeftEvent>(this);
    m_weatherChangeConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&WeatherService::OnWeatherChange>(this);
}

void WeatherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto& partyService = m_world.GetPartyService();
    if (!m_transport.IsConnected() || !partyService.IsInParty())
    {
        m_weatherSyncElapsed = 0.0;
        return;
    }

    // Leader changes arrive as party info updates rather than PartyJoinedEvent.
    if (m_partyLeaderPlayerId != partyService.GetLeaderPlayerId())
        OnPartyJoinedEvent(PartyJoinedEvent(partyService.IsLeader()));

    RunWeatherUpdates(acEvent.Delta);
}

void WeatherService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
    m_partyLeaderPlayerId = 0;
}

void WeatherService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
    const auto& partyService = m_world.GetPartyService();
    m_partyLeaderPlayerId = partyService.GetLeaderPlayerId();
    if (!m_transport.IsConnected() || !partyService.IsInParty())
        return;

    if (!acEvent.IsLeader)
    {
        ToggleGameWeatherSystem(false);
    }
    else
    {
        RunWeatherUpdates(0.0);
    }
}

void WeatherService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
    m_partyLeaderPlayerId = 0;
}

void WeatherService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    const auto& partyService = m_world.GetPartyService();
    if (!m_transport.IsConnected() || !partyService.IsInParty() || partyService.IsLeader() || !m_weatherOverrideActive || !acMessage.Id)
        return;

    auto& modSystem = m_world.GetModSystem();
    const uint32_t weatherId = modSystem.GetGameId(acMessage.Id);
    if (weatherId == 0xA6858)
        return;

    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(weatherId));

    if (!pWeather)
    {
        spdlog::error(__FUNCTION__ ": weather not found, form id: {:X}", acMessage.Id.ModId + acMessage.Id.BaseId);
        return;
    }

    m_cachedWeatherId = weatherId;
    SetCachedWeather();
}

void WeatherService::RunWeatherUpdates(const double acDelta) noexcept
{
    const auto& partyService = m_world.GetPartyService();
    if (!m_transport.IsConnected() || !partyService.IsInParty() || (!partyService.IsLeader() && !m_weatherOverrideActive))
        return;

    if (acDelta > 0.0)
        m_weatherSyncElapsed += acDelta;

    Sky* pSky = Sky::Get();
    if (!pSky)
        return;

    TESWeather* pWeather = pSky->GetWeather();

    // This is the map weather, should not be synced.
    if (pWeather && pWeather->formID == 0xA6858)
        return;

    if (partyService.IsLeader())
    {
        if (!pWeather)
        {
            m_cachedWeatherId = 0;
            return;
        }

        // Check changes each frame; unchanged weather is republished at most once a minute.
        if (pWeather->formID == m_cachedWeatherId && m_weatherSyncElapsed < kWeatherResyncSeconds)
            return;

        RequestWeatherChange request{};

        auto& modSystem = m_world.GetModSystem();
        if (!modSystem.GetServerModId(pWeather->formID, request.Id))
        {
            spdlog::error(__FUNCTION__ ": weather server ID not found, form id: {:X}", pWeather->formID);
            return;
        }

        if (m_transport.Send(request))
        {
            m_cachedWeatherId = pWeather->formID;
            m_weatherSyncElapsed = 0.0;
        }
    }
    else
    {
        if (m_weatherSyncElapsed >= kWeatherResyncSeconds)
        {
            m_transport.Send(RequestCurrentWeather());
            m_weatherSyncElapsed = 0.0;
        }

        if (!pWeather || pWeather->formID != m_cachedWeatherId || m_forcedWeatherId != m_cachedWeatherId)
            SetCachedWeather();
    }
}

void WeatherService::ToggleGameWeatherSystem(bool aToggle) noexcept
{
    m_weatherOverrideActive = !aToggle;
    m_weatherSyncElapsed = 0.0;
    m_cachedWeatherId = 0;
    m_forcedWeatherId = 0;

    if (aToggle)
    {
        if (Sky* pSky = Sky::Get())
            pSky->ReleaseWeatherOverride();
    }
    else
        m_transport.Send(RequestCurrentWeather());
}

void WeatherService::SetCachedWeather() noexcept
{
    if (!m_weatherOverrideActive || m_cachedWeatherId == 0)
        return;

    Sky* pSky = Sky::Get();
    if (!pSky)
        return;

    TESWeather* pCurrentWeather = pSky->GetWeather();
    if (pCurrentWeather)
    {
        if (pCurrentWeather->formID == 0xA6858)
            return;

        // Apply the override once even if the game already has the same weather.
        if (pCurrentWeather->formID == m_cachedWeatherId && m_forcedWeatherId == m_cachedWeatherId)
            return;
    }

    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(m_cachedWeatherId));

    if (!pWeather)
    {
        spdlog::error(__FUNCTION__ ": weather not found, form id: {:X}", m_cachedWeatherId);
        return;
    }

    pSky->ForceWeather(pWeather);
    m_forcedWeatherId = m_cachedWeatherId;
}
