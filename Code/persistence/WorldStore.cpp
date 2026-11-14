#include "WorldStore.h"
#include <sqlite3.h>
#include <cryptopp/sha.h>
#include <algorithm>
#include <array>
#include <limits>
#include <set>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

namespace
{
struct Statement
{
    sqlite3_stmt* Value{};
    Statement(sqlite3* apDb, const char* apSql) noexcept { sqlite3_prepare_v2(apDb, apSql, -1, &Value, nullptr); }
    ~Statement() noexcept { sqlite3_finalize(Value); }
    explicit operator bool() const noexcept { return Value != nullptr; }
};

std::array<uint8_t, 32> Digest(const String& acKey, uint64_t aRevision, const Vector<uint8_t>& acValue) noexcept
{
    CryptoPP::SHA256 hash;
    const std::array<uint8_t, 8> revision{static_cast<uint8_t>(aRevision),       static_cast<uint8_t>(aRevision >> 8),  static_cast<uint8_t>(aRevision >> 16),
                                          static_cast<uint8_t>(aRevision >> 24), static_cast<uint8_t>(aRevision >> 32), static_cast<uint8_t>(aRevision >> 40),
                                          static_cast<uint8_t>(aRevision >> 48), static_cast<uint8_t>(aRevision >> 56)};
    hash.Update(reinterpret_cast<const uint8_t*>(acKey.data()), acKey.size());
    const uint8_t separator = 0;
    hash.Update(&separator, 1);
    hash.Update(revision.data(), revision.size());
    hash.Update(acValue.data(), acValue.size());
    std::array<uint8_t, 32> result{};
    hash.Final(result.data());
    return result;
}

bool ReadRecord(sqlite3_stmt* apStatement, WorldStore::Record& aRecord) noexcept
{
    const int keySize = sqlite3_column_bytes(apStatement, 0);
    const int valueSize = sqlite3_column_bytes(apStatement, 1);
    const int digestSize = sqlite3_column_bytes(apStatement, 2);
    const sqlite3_int64 revision = sqlite3_column_int64(apStatement, 3);
    if (keySize <= 0 || keySize > 512 || valueSize < 0 || static_cast<size_t>(valueSize) > WorldStore::kMaxRecordBytes || digestSize != 32 || revision <= 0)
        return false;
    const auto* pKey = reinterpret_cast<const char*>(sqlite3_column_text(apStatement, 0));
    const auto* pValue = static_cast<const uint8_t*>(sqlite3_column_blob(apStatement, 1));
    const auto* pDigest = static_cast<const uint8_t*>(sqlite3_column_blob(apStatement, 2));
    if (!pKey || !pDigest || (valueSize && !pValue))
        return false;
    aRecord.Key.assign(pKey, keySize);
    aRecord.Value.clear();
    if (valueSize)
        aRecord.Value.assign(pValue, pValue + valueSize);
    aRecord.Revision = static_cast<uint64_t>(revision);
    const auto digest = Digest(aRecord.Key, aRecord.Revision, aRecord.Value);
    return std::equal(digest.begin(), digest.end(), pDigest);
}
} // namespace

WorldStore::~WorldStore() noexcept
{
    Close();
}

bool WorldStore::Fail(const char* apContext) noexcept
{
    m_error = String(apContext) + ": " + (m_db ? sqlite3_errmsg(m_db) : "database is not open");
    return false;
}

bool WorldStore::Execute(const char* apSql) noexcept
{
    if (!m_db || sqlite3_exec(m_db, apSql, nullptr, nullptr, nullptr) != SQLITE_OK)
        return Fail("World database operation failed");
    return true;
}

void WorldStore::Close() noexcept
{
    if (m_db)
        sqlite3_close_v2(m_db);
    m_db = nullptr;
    m_worldId.clear();
    m_revision = 0;
}

bool WorldStore::Open(const std::filesystem::path& acPath) noexcept
{
    Close();
    m_error.clear();
    std::error_code error;
    if (!acPath.parent_path().empty())
        std::filesystem::create_directories(acPath.parent_path(), error);
    if (error)
    {
        m_error = "Cannot create world database directory";
        return false;
    }
    const auto utf8 = acPath.u8string();
    if (sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
    {
        Fail("Cannot open world database");
        Close();
        return false;
    }
    sqlite3_busy_timeout(m_db, 250);
    sqlite3_limit(m_db, SQLITE_LIMIT_LENGTH, static_cast<int>(kMaxRecordBytes + 4096));
    sqlite3_limit(m_db, SQLITE_LIMIT_SQL_LENGTH, 65536);
    if (!Execute("PRAGMA locking_mode=EXCLUSIVE; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA trusted_schema=OFF;"))
    {
        Close();
        return false;
    }
    {
        Statement check(m_db, "PRAGMA quick_check(1)");
        if (!check || sqlite3_step(check.Value) != SQLITE_ROW || String(reinterpret_cast<const char*>(sqlite3_column_text(check.Value, 0))) != "ok")
        {
            m_error = "World database integrity check failed; preserve the database and restore a verified backup";
            Close();
            return false;
        }
    }
    {
        Statement version(m_db, "PRAGMA user_version");
        if (!version || sqlite3_step(version.Value) != SQLITE_ROW)
        {
            Fail("Cannot read world schema");
            Close();
            return false;
        }
        const int schema = sqlite3_column_int(version.Value, 0);
        if (schema != 0 && schema != 1)
        {
            m_error = "Unsupported world database schema; use the matching server build, do not reset the database";
            Close();
            return false;
        }
        Statement application(m_db, "PRAGMA application_id");
        Statement tables(m_db, "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");
        if (!application || !tables || sqlite3_step(application.Value) != SQLITE_ROW || sqlite3_step(tables.Value) != SQLITE_ROW ||
            (schema == 1 && sqlite3_column_int(application.Value, 0) != 1414546256) ||
            (schema == 0 && (sqlite3_column_int(application.Value, 0) != 0 || sqlite3_column_int(tables.Value, 0) != 0)))
        {
            m_error = "The selected file is not a supported True Coop world database";
            Close();
            return false;
        }
    }
    if (!Execute("BEGIN IMMEDIATE;"
                 "CREATE TABLE IF NOT EXISTS identity(id INTEGER PRIMARY KEY CHECK(id=1), world_id TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>=0)) STRICT;"
                 "CREATE TABLE IF NOT EXISTS state(key TEXT PRIMARY KEY, value BLOB NOT NULL, digest BLOB NOT NULL CHECK(length(digest)=32), revision INTEGER NOT NULL "
                 "CHECK(revision>0)) STRICT;"
                 "INSERT OR IGNORE INTO identity VALUES(1,lower(hex(randomblob(16))),0);"
                 "PRAGMA user_version=1; PRAGMA application_id=1414546256; COMMIT;"))
    {
        Close();
        return false;
    }
    Statement identity(m_db, "SELECT world_id,revision FROM identity WHERE id=1");
    if (!identity || sqlite3_step(identity.Value) != SQLITE_ROW || sqlite3_column_bytes(identity.Value, 0) != 32 || sqlite3_column_int64(identity.Value, 1) < 0)
    {
        Fail("Cannot read world identity");
        Close();
        return false;
    }
    m_worldId = reinterpret_cast<const char*>(sqlite3_column_text(identity.Value, 0));
    m_revision = static_cast<uint64_t>(sqlite3_column_int64(identity.Value, 1));
    return true;
}

WorldStore::ReadResult WorldStore::Read(const String& acKey, Record& aRecord) noexcept
{
    if (!m_db)
    {
        Fail("Cannot read world record");
        return ReadResult::Failed;
    }
    Statement statement(m_db, "SELECT key,value,digest,revision FROM state WHERE key=?1");
    if (!statement || sqlite3_bind_text(statement.Value, 1, acKey.c_str(), static_cast<int>(acKey.size()), SQLITE_TRANSIENT) != SQLITE_OK)
    {
        Fail("Cannot prepare world record lookup");
        return ReadResult::Failed;
    }
    const int result = sqlite3_step(statement.Value);
    if (result == SQLITE_DONE)
        return ReadResult::Missing;
    if (result != SQLITE_ROW || !ReadRecord(statement.Value, aRecord) || aRecord.Revision > m_revision)
    {
        m_error = "World record is unreadable or its SHA-256/revision is invalid: " + acKey;
        return ReadResult::Failed;
    }
    return ReadResult::Found;
}

bool WorldStore::ReadPrefix(const String& acPrefix, Vector<Record>& aRecords) noexcept
{
    aRecords.clear();
    if (!m_db || acPrefix.empty() || acPrefix.back() != '/')
        return Fail("Invalid world record prefix");
    String upper = acPrefix;
    ++upper.back();
    Statement statement(m_db, "SELECT key,value,digest,revision FROM state WHERE key>=?1 AND key<?2 ORDER BY revision,key");
    if (!statement || sqlite3_bind_text(statement.Value, 1, acPrefix.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement.Value, 2, upper.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        return Fail("Cannot enumerate world records");
    int result = 0;
    while ((result = sqlite3_step(statement.Value)) == SQLITE_ROW)
    {
        Record record;
        if (!ReadRecord(statement.Value, record) || record.Revision > m_revision || aRecords.size() >= 100000)
        {
            m_error = "World record checksum, revision or count is invalid";
            aRecords.clear();
            return false;
        }
        aRecords.push_back(std::move(record));
    }
    return result == SQLITE_DONE || Fail("Cannot enumerate world records");
}

WorldStore::WriteResult WorldStore::Commit(const Vector<Record>& acRecords, uint64_t& aRevision) noexcept
{
    aRevision = m_revision;
    if (!m_db || acRecords.empty() || acRecords.size() > 4096 || m_revision >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        m_error = "Invalid world transaction or exhausted revision";
        return WriteResult::Failed;
    }
    std::set<String> keys;
    for (const auto& record : acRecords)
        if (record.Key.empty() || record.Key.size() > 512 || record.Key.find('\0') != String::npos || record.Value.size() > kMaxRecordBytes || !keys.insert(record.Key).second)
        {
            m_error = "Invalid or duplicate world transaction key/payload";
            return WriteResult::Failed;
        }
    if (!Execute("BEGIN IMMEDIATE"))
        return WriteResult::Failed;
    struct Rollback
    {
        sqlite3* Db;
        bool Done{};
        ~Rollback()
        {
            if (!Done)
                sqlite3_exec(Db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    } rollback{m_db};
    const uint64_t revision = m_revision + 1;
    for (const auto& record : acRecords)
    {
        Record current;
        const auto read = Read(record.Key, current);
        if (read == ReadResult::Failed)
            return WriteResult::Failed;
        if ((read == ReadResult::Missing ? 0 : current.Revision) != record.Revision)
        {
            m_error = "World record revision conflict: " + record.Key;
            return WriteResult::Conflict;
        }
        const auto digest = Digest(record.Key, revision, record.Value);
        Statement write(m_db, "INSERT INTO state VALUES(?1,?2,?3,?4) ON CONFLICT(key) DO UPDATE SET value=excluded.value,digest=excluded.digest,revision=excluded.revision");
        if (!write || sqlite3_bind_text(write.Value, 1, record.Key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_blob(write.Value, 2, record.Value.empty() ? static_cast<const void*>("") : record.Value.data(), static_cast<int>(record.Value.size()), SQLITE_TRANSIENT) !=
                SQLITE_OK ||
            sqlite3_bind_blob(write.Value, 3, digest.data(), digest.size(), SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int64(write.Value, 4, static_cast<sqlite3_int64>(revision)) != SQLITE_OK || sqlite3_step(write.Value) != SQLITE_DONE)
        {
            Fail("Cannot commit world record");
            return WriteResult::Failed;
        }
    }
    Statement head(m_db, "UPDATE identity SET revision=?1 WHERE id=1");
    if (!head || sqlite3_bind_int64(head.Value, 1, static_cast<sqlite3_int64>(revision)) != SQLITE_OK || sqlite3_step(head.Value) != SQLITE_DONE || sqlite3_changes(m_db) != 1 ||
        !Execute("COMMIT"))
    {
        Fail("Cannot commit world revision");
        return WriteResult::Failed;
    }
    rollback.Done = true;
    m_revision = revision;
    aRevision = revision;
    return WriteResult::Committed;
}
