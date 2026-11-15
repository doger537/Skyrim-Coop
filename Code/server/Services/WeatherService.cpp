#include <Services/WeatherService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/RequestWeatherChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>

WeatherService::WeatherService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_weatherChangeConnection = aDispatcher.sink<PacketEvent<RequestWeatherChange>>().connect<&WeatherService::OnWeatherChange>(this);
    m_currentWeatherConnection = aDispatcher.sink<PacketEvent<RequestCurrentWeather>>().connect<&WeatherService::OnRequestCurrentWeather>(this);
}

void WeatherService::OnWeatherChange(const PacketEvent<RequestWeatherChange>& acMessage) const noexcept
{
    auto& partyService = m_world.GetPartyService();
    if (!partyService.IsPlayerLeader(acMessage.pPlayer) || !acMessage.Packet.Id)
        return;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !m_world.valid(*character) || !m_world.all_of<CharacterComponent, CellIdComponent>(*character))
        return;

    auto* pParty = partyService.GetPlayerParty(acMessage.pPlayer);
    if (!pParty)
        return;

    NotifyWeatherChange notify{};
    notify.Id = acMessage.Packet.Id;
    pParty->CachedWeather = notify.Id;

    GameServer::Get()->SendToPartyInRange(notify, acMessage.pPlayer->GetParty(), *character, acMessage.pPlayer);
}

void WeatherService::OnRequestCurrentWeather(const PacketEvent<RequestCurrentWeather>& acMessage) const noexcept
{
    auto* pParty = m_world.GetPartyService().GetPlayerParty(acMessage.pPlayer);
    if (!pParty || !pParty->CachedWeather)
        return;

    NotifyWeatherChange notify{};
    notify.Id = pParty->CachedWeather;

    acMessage.pPlayer->Send(notify);
}
