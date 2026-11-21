#pragma once

#include "ContentFingerprint.h"

#ifdef _WIN32
#include <compare>
#include <map>
#endif

class ArchiveFingerprintCache
{
public:
    explicit ArchiveFingerprintCache(const std::filesystem::path& acPath) noexcept;

    bool Fingerprint(const std::filesystem::path& acArchive, std::array<uint8_t, 32>& aDigest) noexcept;
    void Save() const noexcept;
    const ArchiveFingerprintStats& Stats() const noexcept { return m_stats; }

private:
    ArchiveFingerprintStats m_stats;

#ifdef _WIN32
    using Digest = std::array<uint8_t, 32>;
    struct FileStamp
    {
        uint64_t Volume{};
        std::array<uint8_t, 16> FileId{};
        uint64_t Size{};
        int64_t Created{}, Written{}, Changed{};
        auto operator<=>(const FileStamp&) const = default;
    };

    void Load() noexcept;

    std::filesystem::path m_path;
    std::map<FileStamp, Digest> m_cached;
    std::map<FileStamp, Digest> m_observed;
#endif
};
