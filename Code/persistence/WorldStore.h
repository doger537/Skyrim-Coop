#pragma once
#include <TiltedCore/Stl.hpp>
#include <cstdint>
#include <filesystem>

struct sqlite3;

// A single server owns a database. Records have stable logical keys, never ECS entity IDs.
class WorldStore
{
public:
    static constexpr size_t kMaxRecordBytes = 4 * 1024 * 1024;
    enum class ReadResult
    {
        Missing,
        Found,
        Failed
    };
    enum class WriteResult
    {
        Committed,
        Conflict,
        Failed
    };
    struct Record
    {
        TiltedPhoques::String Key;
        TiltedPhoques::Vector<uint8_t> Value;
        uint64_t Revision{}; // expected existing revision; zero means the key must be absent
    };

    WorldStore() = default;
    ~WorldStore() noexcept;
    WorldStore(const WorldStore&) = delete;
    WorldStore& operator=(const WorldStore&) = delete;

    bool Open(const std::filesystem::path& acPath) noexcept;
    void Close() noexcept;
    ReadResult Read(const TiltedPhoques::String& acKey, Record& aRecord) noexcept;
    bool ReadPrefix(const TiltedPhoques::String& acPrefix, TiltedPhoques::Vector<Record>& aRecords) noexcept;
    WriteResult Commit(const TiltedPhoques::Vector<Record>& acRecords, uint64_t& aRevision) noexcept;
    const TiltedPhoques::String& Error() const noexcept { return m_error; }
    const TiltedPhoques::String& WorldId() const noexcept { return m_worldId; }
    uint64_t Revision() const noexcept { return m_revision; }
    bool IsOpen() const noexcept { return m_db != nullptr; }

private:
    bool Execute(const char* apSql) noexcept;
    bool Fail(const char* apContext) noexcept;
    sqlite3* m_db{};
    TiltedPhoques::String m_error;
    TiltedPhoques::String m_worldId;
    uint64_t m_revision{};
};
