#include <TiltedOnlinePCH.h>
#include "NativeCheckpoint.h"
#include <Events/WorldCheckpointIntent.h>
#include <Services/TransportService.h>
#include <Interface/UI.h>
#include <Interface/IMenu.h>
#include <Structs/WorldCheckpoint.h>
#include <Forms/TESGlobal.h>
#include <Games/TES.h>
#include <Services/PapyrusService.h>
#include <cmath>
#include <atomic>
#include <array>
#include <cstring>
#include <mutex>

namespace NativeCheckpoint
{
namespace
{
using SaveRequest = bool(__fastcall)(void*, int32_t, uint32_t, const char*);
using LoadRequest = bool(__fastcall)(void*, const char*, int32_t, uint32_t, bool);
using ResultConstructor = void*(__fastcall)(void*, const void*, uint16_t, uint16_t, void**);
SaveRequest* s_save{};
LoadRequest* s_load{};
ResultConstructor* s_result{};
std::atomic<HookMode> s_mode{HookMode::Offline};
thread_local bool s_passthrough{};
struct SaveResult
{
    const String* Name{};
    bool Observed{};
    bool Success{};
};
thread_local SaveResult* s_saveResult{};
std::mutex s_nameMutex;
std::array<char, 260> s_baselineName{};
void RememberLoad(const char* apName) noexcept
{
    const size_t length = apName ? strnlen_s(apName, s_baselineName.size()) : 0;
    const std::lock_guard lock(s_nameMutex);
    s_baselineName.fill(0);
    if (length && length < s_baselineName.size())
        std::memcpy(s_baselineName.data(), apName, length);
}
struct PassThrough
{
    bool Previous{s_passthrough};
    PassThrough() { s_passthrough = true; }
    ~PassThrough() { s_passthrough = Previous; }
};
bool Supported() noexcept { return VersionDb::Get().GetLoadedVersionString() == "1.7.104.0"; }
void* Manager() noexcept
{
    POINTER_SKYRIMSE(void*, manager, 403340);
    return *manager.Get();
}
bool ValidName(const String& acName) noexcept
{
    String world;
    uint64_t id{};
    CharacterIdentity character;
    return CheckpointNames::Parse(std::string_view(acName.data(), acName.size()), world, id, character) && !acName.ends_with(".ess");
}
TESGlobal* TokenGlobal(uint32_t aPart) noexcept
{
    const auto* mod = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!mod || !mod->IsLoaded() || mod->IsLite() || aPart >= 4)
        return nullptr;
    auto* form = TESForm::GetById((uint32_t(mod->standardId) << 24) | (0x4DFF + aPart));
    if (!form || static_cast<uint8_t>(form->formType) != 9 || (form->flags & 64))
        return nullptr;
    auto* global = static_cast<TESGlobal*>(form);
    const String expected = String("TrueCoopCheckpointPart") + std::to_string(aPart).c_str();
    return global->unk1C == 'f' && global->unk14.AsAscii() && expected == global->unk14.AsAscii() ? global : nullptr;
}
void Intent(bool aLoad, uint64_t aId) noexcept
{
    WorldCheckpointIntent event{aLoad, aId, World::Get().GetTransport().GetConnectionGeneration()};
    World::Get().GetRunner().Trigger(event);
}
bool SaveHook(void* apManager, int32_t aKind, uint32_t aStats, const char* apName)
{
    const auto mode = s_mode.load(std::memory_order_relaxed);
    if (!s_passthrough && mode != HookMode::Offline)
    {
        if (mode == HookMode::Busy)
            return false;
        if (mode == HookMode::Host && (aKind == 2 || aKind == 4))
        {
            Intent(false, 0);
            // A manual-overwrite caller deletes its old slot after true. Defer
            // without claiming an overwrite succeeded; our pair gets fresh names.
            return false;
        }
    }
    return s_save(apManager, aKind, aStats, apName);
}
bool LoadHook(void* apManager, const char* apName, int32_t aDevice, uint32_t aStats, bool aCheckMods)
{
    const auto mode = s_mode.load(std::memory_order_relaxed);
    if (!s_passthrough && mode != HookMode::Offline)
    {
        if (mode == HookMode::Busy)
            return false;
        const size_t size = apName ? strnlen_s(apName, 260) : 0;
        String world;
        uint64_t id{};
        CharacterIdentity character;
        if (size && size < 260 && CheckpointNames::Parse(std::string_view(apName, size), world, id, character))
        {
            if (mode == HookMode::Host)
                Intent(true, id);
            return false;
        }
    }
    RememberLoad(apName);
    const bool accepted = s_load(apManager, apName, aDevice, aStats, aCheckMods);
    if (!accepted)
        RememberLoad(nullptr);
    return accepted;
}
void* ResultHook(void* apResult, const void* apOptions, uint16_t aStatus, uint16_t aDetail, void** apFile)
{
    // MCP 14159C580/14159B480: the save writer closes the file, then constructs
    // this result (status 0 success / 2 write error). The constructor consumes
    // *apFile, so inspect its copied, bounded filename before forwarding.
    if (s_saveResult && s_saveResult->Name && apFile && *apFile)
    {
        const char* path = static_cast<const char*>(*apFile) + 0x64;
        const size_t size = strnlen_s(path, 260);
        if (size < 260)
        {
            const std::string_view full(path, size);
            const size_t slash = full.find_last_of("\\/");
            const auto name = full.substr(slash == std::string_view::npos ? 0 : slash + 1);
            const String expected = *s_saveResult->Name + ".ess";
            const String temporary = expected + ".tmp";
            if (name == std::string_view(expected.data(), expected.size()) || name == std::string_view(temporary.data(), temporary.size()))
            {
                s_saveResult->Observed = true;
                s_saveResult->Success = aStatus == 0;
            }
        }
    }
    return s_result(apResult, apOptions, aStatus, aDetail, apFile);
}
void Menu(uint32_t aType) noexcept
{
    POINTER_SKYRIMSE(void*, queue, 400445);
    using AddMessage = void(__fastcall)(void*, const BSFixedString&, uint32_t, void*);
    POINTER_SKYRIMSE(AddMessage, add, 13631);
    const BSFixedString name("Journal Menu");
    if (*queue.Get())
        add.Get()(*queue.Get(), name, aType, nullptr);
}
}
void SetHookMode(HookMode aMode) noexcept { s_mode.store(aMode, std::memory_order_relaxed); }
String BaselineName() noexcept
{
    const std::lock_guard lock(s_nameMutex);
    return String(s_baselineName.data());
}
void RememberCommittedSave(const String& acName) noexcept
{
    if (ValidName(acName))
        RememberLoad(acName.c_str());
}
bool ReadToken(uint64_t& aId) noexcept
{
    if (!Supported())
        return false;
    uint64_t id{};
    for (uint32_t part = 0; part < 4; ++part)
    {
        auto* global = TokenGlobal(part);
        if (!global || !std::isfinite(global->f) || global->f < 0.f || global->f > 65535.f || std::floor(global->f) != global->f)
            return false;
        id |= uint64_t(global->f) << (part * 16);
    }
    aId = id;
    return true;
}
bool WriteToken(uint64_t aId) noexcept
{
    uint64_t current{};
    if (!ReadToken(current))
        return false;
    const auto* function = World::Get().ctx().at<PapyrusService>().Get("GlobalVariable", "SetValue");
    if (!function)
        return false;
    const PapyrusFunction<void, TESGlobal, float> set{function};
    for (uint32_t part = 0; part < 4; ++part)
        set(TokenGlobal(part), float((aId >> (part * 16)) & 0xFFFF));
    return ReadToken(current) && current == aId;
}
std::filesystem::path Path(const String& acName) noexcept
{
    if (!Supported() || !ValidName(acName))
        return {};
    using Utility = void*(__fastcall)();
    using GetPath = void(__fastcall)(void*, const char*, char*, bool, bool);
    POINTER_SKYRIMSE(Utility, utility, 109278);
    POINTER_SKYRIMSE(GetPath, path, 109357);
    std::array<char, 260> result{};
    path.Get()(utility.Get()(), acName.c_str(), result.data(), false, false);
    const size_t size = strnlen_s(result.data(), result.size());
    if (!size || size == result.size())
        return {};
    // Same Windows ANSI encoding as the engine's save-path API; return an owned
    // wide path, never an engine/stack buffer for the asynchronous file reader.
    const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, result.data(), static_cast<int>(size), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, result.data(), static_cast<int>(size), wide.data(), length))
        return {};
    return std::filesystem::path(wide);
}
bool Save(const String& acName) noexcept
{
    if (!Supported() || !ValidName(acName) || !s_save || !Manager() || s_saveResult)
        return false;
    SaveResult result{&acName};
    s_saveResult = &result;
    PassThrough pass;
    const uint64_t started = GetTickCount64();
    const bool accepted = s_save(Manager(), 2, 0, acName.c_str());
    s_saveResult = nullptr;
    spdlog::info("Paired native save: request={}, writer observed={}, writer success={}, capture/write_ms={}",
        accepted, result.Observed, result.Success, GetTickCount64() - started);
    return accepted && result.Observed && result.Success;
}
bool Load(const String& acName) noexcept
{
    if (!Supported() || !ValidName(acName) || !s_load || !Manager())
        return false;
    PassThrough pass;
    RememberLoad(acName.c_str());
    const bool accepted = s_load(Manager(), acName.c_str(), -1, 0, true);
    if (!accepted)
        RememberLoad(nullptr);
    return accepted;
}
bool IsPaused() noexcept
{
    auto* ui = UI::Get();
    const BSFixedString name("Journal Menu");
    auto* menu = ui ? ui->FindMenuByName(name) : nullptr;
    return ui && menu && ui->numPausesGame && menu->PausesGame() && ui->GetMenuOpen(name);
}
bool ShowPause() noexcept
{
    auto* ui = UI::Get();
    const BSFixedString name("Journal Menu");
    if (!Supported() || !ui)
        return false;
    const bool open = ui->GetMenuOpen(name);
    if (!open)
        Menu(UIMessage::kShow);
    return !open;
}
void HidePause() noexcept { if (Supported()) Menu(UIMessage::kHide); }

static TiltedPhoques::Initializer s_hooks([]()
{
    if (!Supported())
        return;
    POINTER_SKYRIMSE(SaveRequest, save, 35727);
    POINTER_SKYRIMSE(LoadRequest, load, 35728);
    POINTER_SKYRIMSE(ResultConstructor, result, 109327);
    s_save = save.Get(); s_load = load.Get(); s_result = result.Get();
    TP_HOOK(&s_save, SaveHook);
    TP_HOOK(&s_load, LoadHook);
    TP_HOOK(&s_result, ResultHook);
});
}
