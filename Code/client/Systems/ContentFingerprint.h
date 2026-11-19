#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/Mods.h>
#include <Structs/WorldCheckpoint.h>

bool FingerprintPluginFile(const std::filesystem::path& acPath, std::array<uint8_t, 32>& aDigest) noexcept;
bool FingerprintCheckpointSave(const std::filesystem::path& acPath, bool aWithCoSave, CheckpointPlayer& aPlayer) noexcept;
// Read only the TES4 header; no engine objects or plugin records cross threads.
bool ReadPluginMasters(const std::filesystem::path& acPath, bool aExpectedLite, Vector<String>& aMasters) noexcept;
struct ArchiveFingerprintStats
{
    size_t Hits{};
    size_t FilesRead{};
    uint64_t BytesRead{};
};
// Strict profile: all installed top-level BSAs, selected loose asset trees, native plugins,
// the actually loaded Address Library and optional SKSE DLL. The optional local
// archive cache changes how BSA digests are obtained, never which digests are sent.
bool FingerprintContent(
    const std::filesystem::path& acDataPath, const std::filesystem::path& acAddressLibrary, const std::filesystem::path& acScriptExtender, Mods& aMods,
    const std::filesystem::path& acArchiveCache = {}, ArchiveFingerprintStats* apStats = nullptr) noexcept;
