
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>

#include <TimeManager.h>
#include <Havok/PhysicsReplication.h>
#include <AI/DialogueMovement.h>

#include <Forms/TESNPC.h>
#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>
#include <steam/isteamnetworkingutils.h>

#include <ScriptExtender.h>
#include <Structs/ContentCompatibility.h>
#include <Systems/ContentFingerprint.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Services/WorldCheckpointService.h>
#include <NativeCheckpoint.h>
#include <NativeQuestContext.h>
#include <bcrypt.h>
#include "../../../immersive_launcher/utils/FileVersion.inl"
#include <Services/DiscordService.h>
#include <Services/QuestService.h>
#include <Messages/NotifyCampaignCheckpoint.h>
#include <Messages/RequestCampaignReady.h>
#include <Messages/RequestCampaignSeed.h>

// #include <imgui_internal.h>

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

using TiltedPhoques::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_connected = false;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
    m_messageHandlers[NotifyCampaignCheckpoint::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto message = TiltedPhoques::CastUnique<NotifyCampaignCheckpoint>(std::move(apMessage));
        HandleCampaignCheckpoint(*message);
    };
}

bool TransportService::Send(const ClientMessage& acMessage, const TiltedPhoques::EPacketFlags aPacketFlags) const noexcept
{
    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen() && acMessage.GetOpcode() != kRequestWorldCheckpoint && acMessage.GetOpcode() != kRequestPhysicsFrame &&
        acMessage.GetOpcode() != kAuthenticationRequest && acMessage.GetOpcode() != kRequestCampaignReady && acMessage.GetOpcode() != kRequestCampaignSeed)
    {
        m_world.ctx().at<WorldCheckpointService>().UnexpectedGameplay();
        return false;
    }
    static thread_local ScratchAllocator s_allocator(1 << 18);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (Client::IsConnected() && (m_connected || acMessage.GetOpcode() == AuthenticationRequest::Opcode ||
                                  (m_pendingAuthentication && (acMessage.GetOpcode() == RequestCampaignReady::Opcode || acMessage.GetOpcode() == RequestCampaignSeed::Opcode))))
    {
        ScopedAllocator _{s_allocator};

        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // Write first byte as packet needs it

        acMessage.Serialize(writer, m_worldEpoch);
        TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

        Client::Send(&packet, aPacketFlags);

        return true;
    }

    return false;
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader, m_worldEpoch);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    if (m_world.ctx().at<WorldCheckpointService>().IsFrozen())
    {
        const auto opcode = pMessage->GetOpcode();
        if (opcode != kNotifyWorldCheckpoint && opcode != kNotifyPhysicsFrame && opcode != kAuthenticationResponse && opcode != kNotifyCampaignCheckpoint &&
            opcode != kNotifyQuestJournal && opcode != kStringCacheUpdate && opcode != kServerTimeSettings && opcode != kNotifySettingsChange && opcode != kNotifyPartyJoined &&
            opcode != kNotifyPartyInfo && opcode != kNotifyPlayerList)
            return;
    }
    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}

void TransportService::OnConnected()
{
    m_world.GetRunner().InvalidateGameplay();
    PhysicsReplication::Reset();
    DialogueMovement::Reset();
    m_worldEpoch = 0;
    m_connected = false;
    m_pendingAuthentication.reset();
    m_campaignNonce = 0;
    m_queuedCheckpointPath.clear();
    ++m_connectionGeneration;
    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.RuntimeVersion = QueryFileVersion((std::filesystem::current_path() / L"SkyrimSE.exe").c_str()).c_str();
    request.SKSEActive = IsScriptExtenderLoaded();
    request.MO2Active = GetModuleHandleW(kMO2DllName);
    if (!NativeCheckpoint::ReadToken(request.CheckpointToken))
    {
        FailCampaign("The paired-save globals are missing. Deploy the complete matching True Coop package and reload your character");
        return;
    }
    const auto identity = SavedCharacterIdentity::Read(request.CharacterId);
    if (identity == SavedCharacterIdentity::Result::Invalid)
    {
        spdlog::warn("Authentication stopped: unreadable saved character identity");
        FailCampaign("Cannot read the character identity. Deploy the complete matching True Coop package, load a save and reconnect");
        return;
    }
    if (identity == SavedCharacterIdentity::Result::Unassigned)
    {
        if (request.CheckpointToken)
        {
            FailCampaign("The paired save is missing its character identity. Load your complete matching checkpoint");
            return;
        }
        auto& words = request.CharacterId.Parts;
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(words.data()), static_cast<ULONG>(sizeof(words)), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
            !request.CharacterId.IsValid())
        {
            spdlog::warn("Authentication stopped: cannot generate a character identity");
            FailCampaign("Cannot generate a character identity. Restart Skyrim and reconnect");
            return;
        }
        if (!SavedCharacterIdentity::Commit(request.CharacterId))
        {
            spdlog::warn("Authentication stopped: native character identity write failed");
            FailCampaign("Cannot initialize the character identity in Skyrim. Reload your character and reconnect");
            return;
        }
    }

    if (request.CheckpointToken)
    {
        const auto loaded = NativeCheckpoint::BaselineName();
        uint64_t id{};
        CharacterIdentity character;
        if (!CheckpointNames::Parse(std::string_view(loaded.data(), loaded.size()), request.CheckpointProof.WorldId, id, character) || id != request.CheckpointToken ||
            character != request.CharacterId)
        {
            FailCampaign("This local save inherited a checkpoint token but was not loaded from its paired checkpoint file. Load your TrueCoop checkpoint save and reconnect");
            return;
        }
        request.CheckpointProof.Player.Character = character;
        const auto name = CheckpointNames::Make(std::string_view(request.CheckpointProof.WorldId.data(), request.CheckpointProof.WorldId.size()), id, character);
        m_queuedCheckpointPath = NativeCheckpoint::Path(name);
        if (m_queuedCheckpointPath.empty())
        {
            FailCampaign("Cannot resolve the loaded paired-save file for verification");
            return;
        }
    }

    request.Token = m_serverPassword;
    m_reconnectPassword = m_serverPassword;
    m_serverPassword = "";

    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;
    auto* pNpc = Cast<TESNPC>(pPlayer->baseForm);
    if (pNpc)
    {
        request.Username = pNpc->fullName.value.AsAscii();
    }
    else
    {
        request.Username = "Some dragon boi";
    }

    auto* const cpModManager = ModManager::Get();

    for (auto* pMod : cpModManager->mods)
    {
        if (!pMod->IsLoaded())
            continue;

        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Id = pMod->GetId();
        entry.IsLite = pMod->IsLite();
        entry.Filename = pMod->filename;
    }

    const bool cWorldMapped = !pPlayer->GetWorldSpace() || ContentCompatibility::EncodeInitialForm(pPlayer->GetWorldSpace()->formID, request.UserMods, request.WorldSpaceId);
    if (!cWorldMapped || !pPlayer->parentCell || !ContentCompatibility::EncodeInitialForm(pPlayer->parentCell->formID, request.UserMods, request.CellId))
    {
        ConnectionErrorEvent event;
        event.ErrorDetail = "{\"error\":\"content_mismatch\",\"data\":{\"reason\":\"Cannot map the current location. Load a save in a supported installed cell and reconnect\"}}";
        m_dispatcher.trigger(event);
        Close();
        return;
    }

    request.Level = pPlayer->GetLevel();

    auto* pGameTime = TimeData::Get();
    request.PlayerTime.TimeScale = pGameTime->TimeScale->f;
    request.PlayerTime.Time = pGameTime->GameHour->f;
    request.PlayerTime.Year = pGameTime->GameYear->f;
    request.PlayerTime.Month = pGameTime->GameMonth->f;
    request.PlayerTime.Day = pGameTime->GameDay->f;
    request.PlayerTime.DaysPassed = pGameTime->GameDaysPassed->f;

    m_queuedAuthentication = std::move(request);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    NativeQuestContext::SetEnabled(false);
    m_deferredCheckpointPlayer.reset();
    m_world.GetRunner().InvalidateGameplay();
    DialogueMovement::Reset();
    PhysicsReplication::Reset();
    m_worldEpoch = 0;
    m_connected = false;
    m_pendingAuthentication.reset();
    m_campaignNonce = 0;
    ++m_connectionGeneration;
    m_queuedAuthentication.reset();
    m_queuedCheckpointPath.clear();

    spdlog::warn("Disconnected from server {}", aReason);

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
    if (m_manifestTask.valid() && m_manifestTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto request = m_manifestTask.get();
        if (m_manifestGeneration == m_connectionGeneration && Client::IsConnected())
        {
            const auto result = ContentCompatibility::Validate(request.UserMods);
            CharacterIdentity current;
            if (SavedCharacterIdentity::Read(current) != SavedCharacterIdentity::Result::Found || current != request.CharacterId)
            {
                spdlog::warn("Authentication stopped: character identity changed while checking game files");
                FailCampaign("The loaded character changed while checking game files. Reconnect after loading finishes");
            }
            else if (request.CheckpointToken && !request.CheckpointProof.IsValid())
                FailCampaign("Cannot verify the loaded paired ESS/SKSE files. Preserve your saves and load a complete matching checkpoint");
            else if (result)
                Send(request);
            else
            {
                ConnectionErrorEvent event;
                event.ErrorDetail = "{\"error\":\"content_mismatch\",\"data\":{\"reason\":\"" + ContentCompatibility::EscapeJson(ContentCompatibility::Describe(result)) + "\"}}";
                m_dispatcher.trigger(event);
                Close();
            }
        }
    }
    if (m_queuedAuthentication && !m_manifestTask.valid())
    {
        auto request = std::move(*m_queuedAuthentication);
        m_queuedAuthentication.reset();
        m_manifestGeneration = m_connectionGeneration;
        const auto dataPath = std::filesystem::current_path() / L"Data";
        auto runtime = request.RuntimeVersion;
        std::replace(runtime.begin(), runtime.end(), '.', '-');
        const auto addressLibrary = dataPath / L"SKSE" / L"Plugins" / (std::string("versionlib-") + runtime.c_str() + ".bin");
        const auto scriptExtender = GetScriptExtenderPath();
        auto checkpointPath = std::move(m_queuedCheckpointPath);
        m_queuedCheckpointPath.clear();
        // Only file IO and copies of the manifest cross threads; no engine objects do.
        m_manifestTask = std::async(
            std::launch::async,
            [request = std::move(request), dataPath, addressLibrary, scriptExtender, checkpointPath = std::move(checkpointPath)]() mutable
            {
                FingerprintContent(dataPath, addressLibrary, scriptExtender, request.UserMods);
                if (request.CheckpointToken)
                    FingerprintCheckpointSave(checkpointPath, request.SKSEActive, request.CheckpointProof.Player);
                if (request.SKSEActive && scriptExtender.empty())
                {
                    auto& missing = request.UserMods.Resources.emplace_back();
                    missing.Filename = "runtime/skse";
                }
                return request;
            });
    }
}

double TransportService::GetMessageReceiveTime() const noexcept
{
    return static_cast<double>(GetCurrentMessageReceiveTime()) / 1000.0;
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_nativeLoadCompleted.exchange(false, std::memory_order_acq_rel))
    {
        m_world.GetRunner().InvalidateGameplay();
        m_queuedAuthentication.reset();
        const bool coordinated = m_world.ctx().at<WorldCheckpointService>().NativeLoadCompleted();
        if (!coordinated && Client::IsConnected())
        {
            // A local .ess load is not a coordinated server rollback. Never
            // continue applying the old session to the newly loaded world.
            ConnectionErrorEvent event;
            event.ErrorDetail =
                "{\"error\":\"content_mismatch\",\"data\":{\"reason\":\"A local save outside the paired checkpoint was loaded. Reconnect after loading finishes\"}}";
            m_dispatcher.trigger(event);
            Close();
        }
    }
    // Taken before polling, so that every message that arrived before this time gets handled by this update
    m_lastPollTime = static_cast<double>(SteamNetworkingUtils()->GetLocalTimestamp()) / 1000.0;

    Update();
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        m_campaignWorldId = acMessage.WorldId;
        if (acMessage.Version != BUILD_COMMIT)
        {
            ConnectionErrorEvent event;
            event.ErrorDetail =
                "{\"error\":\"content_mismatch\",\"data\":{\"reason\":\"Server build differs. Install the same Skyrim True Coop build on all clients and the server\"}}";
            m_dispatcher.trigger(event);
            Close();
            return;
        }
        if (m_pendingAuthentication || m_connected || acMessage.WorldId.size() != 32 || acMessage.WorldEpoch == 0)
        {
            FailCampaign("Invalid or duplicate campaign authentication response");
            return;
        }
        m_worldEpoch = acMessage.WorldEpoch;
        m_pendingAuthentication = acMessage;
        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        // Gameplay begins only after the saved common journal has been checked.
        return; // quit the function here.
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kContentMismatch: ErrorInfo += "\"error\":\"content_mismatch\",\"data\":{\"reason\":\"" + ContentCompatibility::EscapeJson(acMessage.ContentError) + "\"}"; break;
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
        {
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
            ErrorInfo += "\"MO2\"";
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    m_dispatcher.trigger(errorEvent);
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}

void TransportService::FailCampaign(const String& acReason) noexcept
{
    ConnectionErrorEvent event;
    event.ErrorDetail = "{\"error\":\"content_mismatch\",\"data\":{\"reason\":\"" + ContentCompatibility::EscapeJson(acReason) + "\"}}";
    m_dispatcher.trigger(event);
    Close();
}

void TransportService::HandleCampaignCheckpoint(const NotifyCampaignCheckpoint& acMessage) noexcept
{
    if (!m_pendingAuthentication || !acMessage.Decoded || acMessage.WorldId != m_pendingAuthentication->WorldId || !acMessage.Nonce || m_connected)
    {
        FailCampaign("Unexpected shared campaign checkpoint");
        return;
    }
    if (acMessage.State == NotifyCampaignCheckpoint::Begin)
    {
        if (acMessage.Nonce <= m_campaignNonce)
        {
            FailCampaign("Stale shared campaign checkpoint");
            return;
        }
        m_campaignNonce = acMessage.Nonce;
    }
    else if (acMessage.Nonce != m_campaignNonce)
    {
        FailCampaign("Shared campaign checkpoint nonce differs");
        return;
    }
    if (acMessage.State == NotifyCampaignCheckpoint::Accepted)
    {
        String error;
        if (!m_world.ctx().at<QuestService>().ConfirmCampaign(error, &acMessage))
        {
            FailCampaign(error);
            return;
        }
        const uint32_t player = m_pendingAuthentication->PlayerId;
        m_pendingAuthentication.reset();
        m_connected = true;
        m_localPlayerId = player;
        m_world.GetRunner().InvalidateGameplay();
        if (m_world.ctx().at<WorldCheckpointService>().AwaitingReconnect())
            m_deferredCheckpointPlayer = player;
        else
        {
            NativeQuestContext::SetEnabled(true);
            m_dispatcher.trigger(ConnectedEvent(player));
        }
        return;
    }
    m_dispatcher.trigger(acMessage);
}

bool TransportService::Connect(const std::string& acEndpoint) noexcept
{
    m_reconnectEndpoint = acEndpoint;
    return Client::Connect(acEndpoint);
}
void TransportService::ReconnectAfterCheckpoint() noexcept
{
    if (!Client::IsConnected() && !m_reconnectEndpoint.empty())
    {
        m_serverPassword = m_reconnectPassword;
        Client::Connect(m_reconnectEndpoint);
    }
}
void TransportService::CompleteCheckpointConnection() noexcept
{
    if (m_deferredCheckpointPlayer && IsConnected())
    {
        const uint32_t player = *m_deferredCheckpointPlayer;
        m_deferredCheckpointPlayer.reset();
        NativeQuestContext::SetEnabled(true);
        m_dispatcher.trigger(ConnectedEvent(player));
    }
}
