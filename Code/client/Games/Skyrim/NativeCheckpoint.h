#pragma once
#include <TiltedCore/Stl.hpp>
#include <filesystem>

namespace NativeCheckpoint
{
enum class HookMode : uint8_t { Offline, Host, Guest, Busy };
void SetHookMode(HookMode aMode) noexcept;
// Explicit checkpoint operations; engine/runner thread only. No engine pointer
// escapes the call, and worker file hashing uses only the copied path.
bool Save(const TiltedPhoques::String& acName) noexcept;
bool Load(const TiltedPhoques::String& acName) noexcept;
std::filesystem::path Path(const TiltedPhoques::String& acName) noexcept;
bool ShowPause() noexcept;
bool IsPaused() noexcept;
void HidePause() noexcept;
bool ReadToken(uint64_t& aId) noexcept;
bool WriteToken(uint64_t aId) noexcept;
}
