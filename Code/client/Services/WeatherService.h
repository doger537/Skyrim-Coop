#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct NotifyWeatherChange;

/**
 * @brief The party leader supplies weather for the whole party, independent of loaded avatars or cells.
 */
struct WeatherService
{
    WeatherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher);
    ~WeatherService() noexcept = default;

    TP_NOCOPYMOVE(WeatherService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;

    void RunWeatherUpdates(const double acDelta) noexcept;

    void ToggleGameWeatherSystem(bool aToggle) noexcept;
    void SetCachedWeather() noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    /**
    * This variable has two uses:
    * For the party leader, it is used to detect weather changes.
    * For non-leaders, it is used to reapply the server weather if it changes.
    */
    uint32_t m_cachedWeatherId{};
    uint32_t m_forcedWeatherId{};
    uint32_t m_partyLeaderPlayerId{};
    double m_weatherSyncElapsed{};
    bool m_weatherOverrideActive{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_weatherChangeConnection;
};
