#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/Mods.h>

bool FingerprintPluginFile(const std::filesystem::path& acPath, std::array<uint8_t, 32>& aDigest) noexcept;
// Read only the TES4 header; no engine objects or plugin records cross threads.
bool ReadPluginMasters(const std::filesystem::path& acPath, bool aExpectedLite, Vector<String>& aMasters) noexcept;
// Strict profile: all installed top-level BSAs, selected loose asset trees, native plugins,
// the actually loaded Address Library and optional SKSE DLL. No resource bytes are sent.
bool FingerprintContent(
    const std::filesystem::path& acDataPath, const std::filesystem::path& acAddressLibrary, const std::filesystem::path& acScriptExtender, Mods& aMods) noexcept;
