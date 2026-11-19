#include "ArchiveFingerprintCache.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cryptopp/sha.h>
#include <fstream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
constexpr size_t kMaxEntries = 4096;
constexpr std::array<char, 8> kCacheMagic{'T', 'C', 'B', 'S', 'A', '0', '0', '1'};

struct CacheHeader
{
    std::array<char, 8> Magic;
    uint64_t Count{};
    std::array<uint8_t, 32> Digest{};
};
static_assert(sizeof(CacheHeader) == 48 && std::has_unique_object_representations_v<CacheHeader>);

struct FileHandle
{
    HANDLE Handle;
    explicit FileHandle(HANDLE aHandle) noexcept
        : Handle(aHandle)
    {
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle()
    {
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }
    explicit operator bool() const noexcept { return Handle != INVALID_HANDLE_VALUE; }
};

bool CanCache(HANDLE aFile) noexcept
{
    // Cache only where the file ID and change times have local NTFS semantics.
    wchar_t fileSystem[16]{};
    FILE_REMOTE_PROTOCOL_INFO remote{};
    return GetVolumeInformationByHandleW(aFile, nullptr, 0, nullptr, nullptr, nullptr, fileSystem, std::size(fileSystem)) && std::wstring_view(fileSystem) == L"NTFS" &&
           !GetFileInformationByHandleEx(aFile, FileRemoteProtocolInfo, &remote, sizeof(remote));
}

template <class T> bool ReadStamp(HANDLE aFile, T& aStamp) noexcept
{
    FILE_ID_INFO id{};
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    if (!GetFileInformationByHandleEx(aFile, FileIdInfo, &id, sizeof(id)) || !GetFileInformationByHandleEx(aFile, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(aFile, FileStandardInfo, &standard, sizeof(standard)) || standard.Directory || standard.DeletePending || standard.EndOfFile.QuadPart < 0 ||
        basic.CreationTime.QuadPart <= 0 || basic.LastWriteTime.QuadPart <= 0 || basic.ChangeTime.QuadPart <= 0)
        return false;

    aStamp.Volume = id.VolumeSerialNumber;
    std::copy_n(id.FileId.Identifier, aStamp.FileId.size(), aStamp.FileId.begin());
    aStamp.Size = static_cast<uint64_t>(standard.EndOfFile.QuadPart);
    aStamp.Created = basic.CreationTime.QuadPart;
    aStamp.Written = basic.LastWriteTime.QuadPart;
    aStamp.Changed = basic.ChangeTime.QuadPart;
    return true;
}

bool WriteBytes(HANDLE aFile, const void* apData, DWORD aBytes) noexcept
{
    DWORD written{};
    return WriteFile(aFile, apData, aBytes, &written, nullptr) && written == aBytes;
}

template <class Entry> std::array<uint8_t, 32> EntriesDigest(const std::vector<Entry>& acEntries) noexcept
{
    std::array<uint8_t, 32> digest{};
    CryptoPP::SHA256 hash;
    hash.CalculateDigest(digest.data(), reinterpret_cast<const uint8_t*>(acEntries.data()), acEntries.size() * sizeof(Entry));
    return digest;
}
} // namespace
#endif

ArchiveFingerprintCache::ArchiveFingerprintCache(const std::filesystem::path& acPath) noexcept
#ifdef _WIN32
    : m_path(acPath)
#endif
{
#ifdef _WIN32
    if (!m_path.empty())
        Load();
#else
    (void)acPath;
#endif
}

#ifdef _WIN32
void ArchiveFingerprintCache::Load() noexcept
{
    struct Entry
    {
        FileStamp Stamp;
        Digest Hash;
    };
    static_assert(sizeof(Entry) == 88 && std::has_unique_object_representations_v<Entry>);

    std::ifstream input(m_path, std::ios::binary);
    CacheHeader header{};
    if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.Magic != kCacheMagic || !header.Count || header.Count > kMaxEntries)
        return;

    std::vector<Entry> entries(static_cast<size_t>(header.Count));
    if (!input.read(reinterpret_cast<char*>(entries.data()), entries.size() * sizeof(Entry)) || input.peek() != std::char_traits<char>::eof() || input.bad() ||
        EntriesDigest(entries) != header.Digest)
        return;

    for (const auto& entry : entries)
        m_cached.emplace(entry.Stamp, entry.Hash);
}
#endif

bool ArchiveFingerprintCache::Fingerprint(const std::filesystem::path& acArchive, std::array<uint8_t, 32>& aDigest) noexcept
{
    aDigest.fill(0);
#ifdef _WIN32
    if (!m_path.empty())
    {
        // Keep reads and the metadata snapshot on the same handle. Denying
        // write/delete sharing prevents editing or replacing it during this step.
        const FileHandle file(CreateFileW(acArchive.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file)
            return false;

        FileStamp stamp{};
        const bool cacheable = CanCache(file.Handle) && ReadStamp(file.Handle, stamp);
        if (cacheable)
        {
            if (const auto found = m_cached.find(stamp); found != m_cached.end())
            {
                aDigest = found->second;
                if (m_observed.size() < kMaxEntries)
                    m_observed.emplace(stamp, aDigest);
                ++m_stats.Hits;
                return true;
            }
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.Handle, &size) || size.QuadPart < 0)
            return false;
        CryptoPP::SHA256 hash;
        std::array<uint8_t, 128 * 1024> buffer;
        uint64_t total{};
        DWORD bytes{};
        do
        {
            if (!ReadFile(file.Handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr))
                return false;
            hash.Update(buffer.data(), bytes);
            total += bytes;
        } while (bytes);
        if (total != static_cast<uint64_t>(size.QuadPart))
            return false;
        FileStamp after{};
        if (cacheable && (!ReadStamp(file.Handle, after) || after != stamp || total != stamp.Size))
            return false;

        hash.Final(aDigest.data());
        if (cacheable && m_observed.size() < kMaxEntries)
            m_observed.emplace(stamp, aDigest);
        ++m_stats.FilesRead;
        m_stats.BytesRead += total;
        return true;
    }
#endif
    if (!FingerprintPluginFile(acArchive, aDigest))
        return false;
    ++m_stats.FilesRead;
    std::error_code error;
    const auto size = std::filesystem::file_size(acArchive, error);
    if (!error)
        m_stats.BytesRead += size;
    return true;
}

void ArchiveFingerprintCache::Save() const noexcept
{
#ifdef _WIN32
    if (m_path.empty() || m_observed.empty() || m_observed == m_cached)
        return;

    struct Entry
    {
        FileStamp Stamp;
        Digest Hash;
    };
    static_assert(sizeof(Entry) == 88 && std::has_unique_object_representations_v<Entry>);
    std::vector<Entry> entries;
    entries.reserve(m_observed.size());
    for (const auto& [stamp, digest] : m_observed)
        entries.push_back({stamp, digest});
    const CacheHeader header{kCacheMagic, entries.size(), EntriesDigest(entries)};

    std::error_code error;
    if (!m_path.parent_path().empty())
        std::filesystem::create_directories(m_path.parent_path(), error);
    if (error)
        return;
    auto temporary = m_path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    bool written{};
    {
        const FileHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
            return;
        written = WriteBytes(file.Handle, &header, sizeof(header)) && WriteBytes(file.Handle, entries.data(), static_cast<DWORD>(entries.size() * sizeof(Entry))) &&
                  FlushFileBuffers(file.Handle);
    }
    if (!written || !MoveFileExW(temporary.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temporary.c_str());
#endif
}
