#include <Services/WorldCheckpointService.h>
#include <Services/TransportService.h>
#include <Services/CalendarService.h>
#include <World.h>
#include <NativeCheckpoint.h>
#include <Forms/SavedCharacterIdentity.h>
#include <Systems/ContentFingerprint.h>
#include <Events/WorldCheckpointIntent.h>
#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <ScriptExtender.h>
#include <thread>

namespace
{
bool InspectFile(const std::filesystem::path& acPath, CheckpointFile& aFile) noexcept
{
    std::error_code error;
    const auto bytes = std::filesystem::file_size(acPath, error);
    if (error || !bytes || bytes > CheckpointFile::kMaxBytes || !FingerprintPluginFile(acPath, aFile.Digest))
        return false;
    aFile.Bytes = bytes;
    return aFile.IsValid();
}
}
WorldCheckpointService::WorldCheckpointService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_transport(aTransport)
{
    m_intentConnection = aDispatcher.sink<WorldCheckpointIntent>().connect<&WorldCheckpointService::OnIntent>(this);
    m_notifyConnection = aDispatcher.sink<NotifyWorldCheckpoint>().connect<&WorldCheckpointService::OnNotify>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WorldCheckpointService::Tick>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&WorldCheckpointService::OnDisconnected>(this);
}
void WorldCheckpointService::Notice(const char* apMessage) noexcept { m_world.GetOverlayService().SendSystemMessage(apMessage); }
void WorldCheckpointService::Send(RequestWorldCheckpoint::Kind aKind) noexcept
{
    RequestWorldCheckpoint message;
    message.Action = aKind; message.Id = m_control.Id; message.Nonce = m_control.Nonce;
    m_transport.Send(message);
}
void WorldCheckpointService::OnIntent(const WorldCheckpointIntent& acEvent) noexcept
{
    if (m_active || !m_transport.IsConnected() || acEvent.ConnectionGeneration != m_transport.GetConnectionGeneration())
        return;
    const auto& party = m_world.GetPartyService();
    if (!party.IsInParty() || !party.IsLeader() || party.GetPartyMembers().size() != 2)
    {
        Notice("Для общего сохранения нужна группа из двух игроков; сохраняет и загружает лидер."); return;
    }
    RequestWorldCheckpoint request;
    request.Action = acEvent.Load ? RequestWorldCheckpoint::Kind::Load : RequestWorldCheckpoint::Kind::Save;
    request.Id = acEvent.Id;
    m_transport.Send(request);
}
void WorldCheckpointService::OnNotify(const NotifyWorldCheckpoint& acMessage) noexcept
{
    if (!acMessage.IsValid())
        return;
    const uint64_t generation = m_transport.GetConnectionGeneration();
    m_world.GetRunner().Queue([this, message = acMessage, generation]()
    {
        if (generation == m_transport.GetConnectionGeneration())
            Apply(message);
    });
}
void WorldCheckpointService::Apply(const NotifyWorldCheckpoint& acMessage) noexcept
{
    using Phase = NotifyWorldCheckpoint::Phase;
    if (acMessage.WorldId != m_transport.GetCampaignWorldId())
        return;
    if (acMessage.State == Phase::Cancelled)
    {
        if (!m_active || acMessage.Nonce == m_control.Nonce)
        {
            Notice("Общее сохранение/загрузка отменены: проверьте группу, файлы сохранений и журнал сервера.");
            Finish(false);
        }
        return;
    }
    if (acMessage.State == Phase::Resume)
    {
        if (AwaitingReconnect() && acMessage.Id == m_control.Id && acMessage.Members == m_control.Members &&
            acMessage.PreviousEpoch == m_transport.GetWorldEpoch() && m_transport.IsConnected())
        {
            Notice("Оба игрока вернулись в общий мир после загрузки.");
            Finish(true);
            m_transport.CompleteCheckpointConnection();
        }
        return;
    }
    if (acMessage.State == Phase::PrepareSave || acMessage.State == Phase::PrepareLoad)
    {
        if (m_active || acMessage.PreviousEpoch != m_transport.GetWorldEpoch() || !m_transport.IsConnected() || m_inspection.valid())
            return;
        CharacterIdentity character;
        if (SavedCharacterIdentity::Read(character) != SavedCharacterIdentity::Result::Found || !NativeCheckpoint::ReadToken(m_previousToken))
            return;
        m_slot = acMessage.Members[0] == character ? 0 : acMessage.Members[1] == character ? 1 : 2;
        if (m_slot == 2)
            return;
        m_control = acMessage;
        m_active = true;
        m_deadline = GetTickCount64() + 85000;
        m_prepared = m_inspected = m_wroteToken = false;
        m_dirty = false;
        NativeCheckpoint::SetHookMode(NativeCheckpoint::HookMode::Busy);
        m_openedPause = NativeCheckpoint::ShowPause();
        Notice(acMessage.State == Phase::PrepareSave ? "Готовим общее сохранение. Дождитесь второго игрока." : "Проверяем оба файла перед общей загрузкой.");
        return;
    }
    if (!m_active || acMessage.Nonce != m_control.Nonce || acMessage.Id != m_control.Id || acMessage.Members != m_control.Members)
        return;
    if (acMessage.State == Phase::SaveNow && m_control.State == Phase::PrepareSave && m_prepared)
    {
        m_control = acMessage;
        m_world.ctx().at<CalendarService>().FreezeForCheckpoint(m_control.Calendar);
        m_wroteToken = NativeCheckpoint::WriteToken(m_control.Id);
        const auto name = CheckpointNames::Make(std::string_view(m_control.WorldId.data(), m_control.WorldId.size()), m_control.Id, m_control.Members[m_slot]);
        if (!m_wroteToken || !NativeCheckpoint::IsPaused() || !NativeCheckpoint::Save(name))
        {
            Send(RequestWorldCheckpoint::Kind::Failed); Finish(false); return;
        }
        Inspect(false);
    }
    else if (acMessage.State == Phase::Saved && m_control.State == Phase::SaveNow)
    {
        Notice(fmt::format("Общее сохранение №{} готово. Загрузка лидером: /coop-load {}", m_control.Id, m_control.Id).c_str());
        Finish(true);
    }
    else if (acMessage.State == Phase::LoadNow && m_control.State == Phase::PrepareLoad && m_prepared && m_inspected &&
        acMessage.PreviousEpoch == m_transport.GetWorldEpoch() && acMessage.WorldEpoch > acMessage.PreviousEpoch)
    {
        m_control = acMessage;
        m_loading = true;
        m_deadline = GetTickCount64() + 180000;
        const auto name = CheckpointNames::Make(std::string_view(m_control.WorldId.data(), m_control.WorldId.size()), m_control.Id, m_control.Members[m_slot]);
        m_transport.Close(); // Native entity cleanup happens BEFORE reading the new .ess.
        if (!NativeCheckpoint::Load(name))
        {
            Notice("Skyrim отказался загружать общий сейв. Сохранения не заменялись; проверьте журнал.");
            Finish(false);
        }
    }
}
void WorldCheckpointService::Inspect(bool aLoad) noexcept
{
    if (m_inspection.valid())
        return;
    const auto name = CheckpointNames::Make(std::string_view(m_control.WorldId.data(), m_control.WorldId.size()), m_control.Id, m_control.Members[m_slot]);
    const auto path = NativeCheckpoint::Path(name);
    const auto expected = aLoad ? m_control.Manifest.Players[m_slot] : CheckpointPlayer{};
    const auto character = m_control.Members[m_slot];
    const uint64_t nonce = m_control.Nonce;
    const bool coSave = IsScriptExtenderLoaded();
    m_cancelInspection = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_cancelInspection;
    m_inspection = std::async(std::launch::async, [path, expected, character, nonce, aLoad, coSave, cancel]()
    {
        Inspection result;
        result.Nonce = nonce; result.Load = aLoad; result.Player.Character = character;
        auto companion = path; companion.replace_extension(L".skse");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(aLoad ? 1 : 30);
        do
        {
            CheckpointPlayer first; first.Character = character;
            if (!path.empty() && InspectFile(path, first.Save) && (!coSave || InspectFile(companion, first.CoSave)))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                CheckpointPlayer second; second.Character = character;
                if (InspectFile(path, second.Save) && (!coSave || InspectFile(companion, second.CoSave)) && first == second)
                {
                    result.Player = second;
                    result.Valid = !aLoad || second == expected;
                    return result;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        } while (!cancel->load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline);
        return result;
    });
}
void WorldCheckpointService::Finish(bool aSuccess) noexcept
{
    if (m_cancelInspection)
        m_cancelInspection->store(true, std::memory_order_relaxed);
    if (!aSuccess && m_wroteToken)
        NativeCheckpoint::WriteToken(m_previousToken);
    if (m_openedPause)
        NativeCheckpoint::HidePause();
    m_world.ctx().at<CalendarService>().ThawCheckpoint();
    m_active = m_frozen = m_openedPause = m_prepared = m_loading = m_loaded = m_inspected = m_wroteToken = false;
    m_control = {};
    NativeCheckpoint::SetHookMode(NativeCheckpoint::HookMode::Offline);
}
void WorldCheckpointService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    if (!m_loading)
        Finish(false);
}
bool WorldCheckpointService::NativeLoadCompleted() noexcept
{
    if (!m_loading || m_loaded)
        return false;
    CharacterIdentity character;
    uint64_t token{};
    if (!NativeCheckpoint::ReadToken(token) || token != m_control.Id || SavedCharacterIdentity::Read(character) != SavedCharacterIdentity::Result::Found ||
        character != m_control.Members[m_slot])
    {
        Notice("Загруженный персонаж или номер общего сейва не совпадает. Автоподключение остановлено.");
        Finish(false); return true;
    }
    m_loaded = true;
    m_openedPause = NativeCheckpoint::ShowPause() || m_openedPause;
    m_nextReconnect = GetTickCount64() + 500;
    return true;
}
void WorldCheckpointService::Tick(const UpdateEvent&) noexcept
{
    const uint64_t now = GetTickCount64();
    if (m_active && m_dirty)
    {
        Send(RequestWorldCheckpoint::Kind::Failed);
        Notice("Мир ещё изменяется после паузы. Общее сохранение отменено; дождитесь завершения сцены и повторите.");
        Finish(false);
    }
    if (m_inspection.valid() && m_inspection.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const auto result = m_inspection.get();
        if (m_active && result.Nonce == m_control.Nonce)
        {
            if (!result.Valid)
            {
                Send(RequestWorldCheckpoint::Kind::Failed); Finish(false);
                Notice("Файл общего сейва отсутствует, изменён или не завершён. Загрузка не началась.");
            }
            else if (result.Load)
            {
                m_inspected = true; m_prepared = true;
                Send(RequestWorldCheckpoint::Kind::Prepared);
            }
            else
            {
                RequestWorldCheckpoint request;
                request.Action = RequestWorldCheckpoint::Kind::Saved;
                request.Id = m_control.Id; request.Nonce = m_control.Nonce; request.Contribution = result.Player;
                m_transport.Send(request);
            }
        }
    }
    if (!m_active)
    {
        if (m_inspection.valid())
        {
            NativeCheckpoint::SetHookMode(NativeCheckpoint::HookMode::Busy);
            return;
        }
        const auto& party = m_world.GetPartyService();
        NativeCheckpoint::SetHookMode(!m_transport.IsConnected() ? NativeCheckpoint::HookMode::Offline :
            party.IsInParty() && party.IsLeader() && party.GetPartyMembers().size() == 2 ? NativeCheckpoint::HookMode::Host : NativeCheckpoint::HookMode::Guest);
        return;
    }
    if (now >= m_deadline)
    {
        const bool loading = m_loading;
        if (m_transport.IsConnected()) Send(RequestWorldCheckpoint::Kind::Failed);
        Notice("Время ожидания общей загрузки/сохранения истекло. Проверьте второго игрока и сервер.");
        Finish(false);
        // Admission may have completed while ConnectedEvent is deliberately
        // deferred. Never leave an admitted but unassigned gameplay session.
        if (loading)
            m_transport.Close();
        return;
    }
    if (m_loading)
    {
        if (m_loaded && !NativeCheckpoint::IsPaused())
        {
            m_openedPause = NativeCheckpoint::ShowPause() || m_openedPause;
            return;
        }
        if (m_loaded && !m_transport.IsConnected() && now >= m_nextReconnect)
        {
            m_transport.ReconnectAfterCheckpoint();
            m_nextReconnect = now + 5000;
        }
        return;
    }
    if (!m_prepared && NativeCheckpoint::IsPaused())
    {
        m_frozen = true;
        m_world.GetRunner().InvalidateGameplay();
        if (m_control.State == NotifyWorldCheckpoint::Phase::PrepareLoad)
        {
            if (!m_inspection.valid()) Inspect(true);
        }
        else
        {
            m_prepared = true;
            Send(RequestWorldCheckpoint::Kind::Prepared);
        }
    }
    else if (m_frozen && !NativeCheckpoint::IsPaused())
    {
        Send(RequestWorldCheckpoint::Kind::Failed); Finish(false);
    }
}
void WorldCheckpointService::UnexpectedGameplay() noexcept
{
    if (m_active && (m_control.State == NotifyWorldCheckpoint::Phase::PrepareSave || m_control.State == NotifyWorldCheckpoint::Phase::SaveNow))
        m_dirty = true;
}
