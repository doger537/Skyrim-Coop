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
    if (!Execute("PRAGMA locking_mode=EXCLUSIVE; PRAGMA trusted_schema=OFF;"))
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
        if (schema != 0 && schema != 1 && schema != 2)
        {
            m_error = "Unsupported world database schema; use the matching server build, do not reset the database";
            Close();
            return false;
        }
        Statement application(m_db, "PRAGMA application_id");
        Statement tables(m_db, "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");
        if (!application || !tables || sqlite3_step(application.Value) != SQLITE_ROW || sqlite3_step(tables.Value) != SQLITE_ROW ||
            (schema != 0 && sqlite3_column_int(application.Value, 0) != 1414546256) ||
            (schema == 0 && (sqlite3_column_int(application.Value, 0) != 0 || sqlite3_column_int(tables.Value, 0) != 0)))
        {
            m_error = "The selected file is not a supported True Coop world database";
            Close();
            return false;
        }
    }
    if (!Execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; BEGIN IMMEDIATE;"
                 "CREATE TABLE IF NOT EXISTS identity(id INTEGER PRIMARY KEY CHECK(id=1), world_id TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>=0)) STRICT;"
                 "CREATE TABLE IF NOT EXISTS state(key TEXT PRIMARY KEY, value BLOB NOT NULL, digest BLOB NOT NULL CHECK(length(digest)=32), revision INTEGER NOT NULL "
                 "CHECK(revision>0)) STRICT;"
                 "CREATE TABLE IF NOT EXISTS checkpoints(id INTEGER PRIMARY KEY CHECK(id>0), captured_revision INTEGER NOT NULL, metadata BLOB NOT NULL, digest BLOB NOT NULL CHECK(length(digest)=32)) STRICT;"
                 "CREATE TABLE IF NOT EXISTS checkpoint_state(checkpoint_id INTEGER NOT NULL, key TEXT NOT NULL, value BLOB NOT NULL, digest BLOB NOT NULL CHECK(length(digest)=32), revision INTEGER NOT NULL CHECK(revision>0), PRIMARY KEY(checkpoint_id,key)) STRICT;"
                 "INSERT OR IGNORE INTO identity VALUES(1,lower(hex(randomblob(16))),0);"
                 "PRAGMA user_version=2; PRAGMA application_id=1414546256; COMMIT;"))
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

bool WorldStore::VerifyRecords() noexcept
{
    if (!m_db)
        return Fail("Cannot verify a closed world");
    Statement statement(m_db, "SELECT key,value,digest,revision FROM state ORDER BY key");
    if (!statement)
        return Fail("Cannot prepare world integrity scan");
    int result = 0;
    while ((result = sqlite3_step(statement.Value)) == SQLITE_ROW)
    {
        Record record;
        if (!ReadRecord(statement.Value, record) || record.Revision > m_revision)
        {
            m_error = "World record SHA-256 or revision is invalid; restore a verified backup";
            return false;
        }
    }
    return result == SQLITE_DONE || Fail("Cannot complete world integrity scan");
}

namespace
{
constexpr size_t kMaxCheckpointRecords = 65536;
constexpr size_t kMaxCheckpointBytes = 64 * 1024 * 1024;
bool OutsideTimeline(const String& acKey) noexcept
{
    return acKey == "$world-epoch" || acKey == "$restored-checkpoint" || acKey == "$restore-pending" || acKey.starts_with("$checkpoint-reservation/") || acKey.starts_with("$participant/");
}
String ReservationKey(uint64_t aId) noexcept
{
    return String("$checkpoint-reservation/") + std::to_string(aId).c_str();
}
std::array<uint8_t, 32> CheckpointDigest(const String& acWorld, const WorldStore::Checkpoint& acCheckpoint) noexcept
{
    CryptoPP::SHA256 hash;
    const auto seed = Digest(acWorld + "/checkpoint/" + std::to_string(acCheckpoint.Id).c_str(), acCheckpoint.CapturedRevision, acCheckpoint.Metadata);
    hash.Update(seed.data(), seed.size());
    for (const auto& record : acCheckpoint.Records)
    {
        const auto digest = Digest(record.Key, record.Revision, record.Value);
        hash.Update(digest.data(), digest.size());
    }
    std::array<uint8_t, 32> result{};
    hash.Final(result.data());
    return result;
}
}

bool WorldStore::ReserveCheckpoint(uint64_t& aId) noexcept
{
    aId = 0;
    if (m_revision >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return Fail("Checkpoint identity exhausted");
    const uint64_t id = m_revision + 1;
    uint64_t revision{};
    if (Commit({{ReservationKey(id), {'T', 'C', 'R', 1}, 0}}, revision) != WriteResult::Committed)
        return false;
    aId = revision;
    return true;
}

bool WorldStore::CaptureCheckpoint(uint64_t aId, const Vector<uint8_t>& acMetadata) noexcept
{
    Record reservation;
    if (!m_db || aId == 0 || aId > m_revision || m_revision >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        acMetadata.empty() || acMetadata.size() > 65536 || Read(ReservationKey(aId), reservation) != ReadResult::Found ||
        reservation.Revision != aId || reservation.Value != Vector<uint8_t>{'T', 'C', 'R', 1})
        return Fail("Checkpoint reservation or metadata is invalid");
    Checkpoint checkpoint{aId, m_revision, acMetadata, {}};
    Statement rows(m_db, "SELECT key,value,digest,revision FROM state ORDER BY key");
    if (!rows)
        return Fail("Cannot enumerate the checkpoint timeline");
    size_t bytes = acMetadata.size();
    int result{};
    while ((result = sqlite3_step(rows.Value)) == SQLITE_ROW)
    {
        Record record;
        if (!ReadRecord(rows.Value, record) || record.Revision > m_revision)
            return Fail("Cannot capture an unreadable world record");
        if (OutsideTimeline(record.Key))
            continue;
        bytes += record.Key.size() + record.Value.size() + 40;
        if (checkpoint.Records.size() >= kMaxCheckpointRecords || bytes > kMaxCheckpointBytes)
            return Fail("World checkpoint exceeds the supported size");
        checkpoint.Records.push_back(std::move(record));
    }
    if (result != SQLITE_DONE)
        return Fail("Cannot enumerate the checkpoint timeline");
    const auto digest = CheckpointDigest(m_worldId, checkpoint);
    if (!Execute("BEGIN IMMEDIATE"))
        return false;
    struct Rollback { sqlite3* Db; bool Done{}; ~Rollback() { if (!Done) sqlite3_exec(Db, "ROLLBACK", nullptr, nullptr, nullptr); } } rollback{m_db};
    Statement insert(m_db, "INSERT INTO checkpoints VALUES(?1,?2,?3,?4)");
    if (!insert || sqlite3_bind_int64(insert.Value, 1, aId) != SQLITE_OK || sqlite3_bind_int64(insert.Value, 2, m_revision) != SQLITE_OK ||
        sqlite3_bind_blob(insert.Value, 3, acMetadata.data(), static_cast<int>(acMetadata.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(insert.Value, 4, digest.data(), 32, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_step(insert.Value) != SQLITE_DONE)
        return Fail("Cannot commit the checkpoint manifest (existing IDs cannot be replaced)");
    Statement copy(m_db, "INSERT INTO checkpoint_state SELECT ?1,key,value,digest,revision FROM state WHERE key!='$world-epoch' AND key!='$restored-checkpoint' AND key!='$restore-pending' AND key NOT GLOB '$checkpoint-reservation/*' AND key NOT GLOB '$participant/*'");
    if (!copy || sqlite3_bind_int64(copy.Value, 1, aId) != SQLITE_OK || sqlite3_step(copy.Value) != SQLITE_DONE ||
        !Execute("UPDATE identity SET revision=revision+1 WHERE id=1; COMMIT"))
        return Fail("Cannot commit the checkpoint timeline");
    rollback.Done = true;
    ++m_revision;
    return true;
}

bool WorldStore::ReadCheckpoint(uint64_t aId, Checkpoint& aCheckpoint) noexcept
{
    if (!m_db || aId == 0 || aId > m_revision)
        return Fail("Invalid checkpoint identity");
    Statement header(m_db, "SELECT captured_revision,metadata,digest FROM checkpoints WHERE id=?1");
    if (!header || sqlite3_bind_int64(header.Value, 1, aId) != SQLITE_OK || sqlite3_step(header.Value) != SQLITE_ROW ||
        sqlite3_column_int64(header.Value, 0) < static_cast<int64_t>(aId) || sqlite3_column_int64(header.Value, 0) > static_cast<int64_t>(m_revision) ||
        sqlite3_column_bytes(header.Value, 1) < 1 || sqlite3_column_bytes(header.Value, 1) > 65536 || sqlite3_column_bytes(header.Value, 2) != 32)
        return Fail("Checkpoint manifest is missing or invalid");
    Checkpoint checkpoint;
    checkpoint.Id = aId;
    checkpoint.CapturedRevision = static_cast<uint64_t>(sqlite3_column_int64(header.Value, 0));
    const auto* metadata = static_cast<const uint8_t*>(sqlite3_column_blob(header.Value, 1));
    checkpoint.Metadata.assign(metadata, metadata + sqlite3_column_bytes(header.Value, 1));
    std::array<uint8_t, 32> expected{};
    std::memcpy(expected.data(), sqlite3_column_blob(header.Value, 2), 32);
    Statement rows(m_db, "SELECT key,value,digest,revision FROM checkpoint_state WHERE checkpoint_id=?1 ORDER BY key");
    if (!rows || sqlite3_bind_int64(rows.Value, 1, aId) != SQLITE_OK)
        return Fail("Cannot enumerate checkpoint records");
    int result{};
    size_t bytes = checkpoint.Metadata.size();
    while ((result = sqlite3_step(rows.Value)) == SQLITE_ROW)
    {
        Record record;
        if (!ReadRecord(rows.Value, record) || record.Revision > checkpoint.CapturedRevision || OutsideTimeline(record.Key))
            return Fail("Checkpoint contains an unreadable or forbidden timeline record");
        bytes += record.Key.size() + record.Value.size() + 40;
        if (checkpoint.Records.size() >= kMaxCheckpointRecords || bytes > kMaxCheckpointBytes)
            return Fail("Checkpoint timeline exceeds the supported size");
        checkpoint.Records.push_back(std::move(record));
    }
    if (result != SQLITE_DONE || expected != CheckpointDigest(m_worldId, checkpoint))
        return Fail("Checkpoint timeline checksum differs; preserve the database");
    aCheckpoint = std::move(checkpoint);
    return true;
}

bool WorldStore::RestoreCheckpoint(uint64_t aId, uint64_t& aNewEpoch) noexcept
{
    aNewEpoch = 0;
    Checkpoint checkpoint;
    if (!ReadCheckpoint(aId, checkpoint) || m_revision >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return false;
    // A checkpoint cannot change the world's installed content or adapter policy.
    for (const char* key : {"$contract", "$adapters"})
    {
        const auto saved = std::find_if(checkpoint.Records.begin(), checkpoint.Records.end(), [key](const Record& record) { return record.Key == key; });
        Record current;
        const auto read = Read(key, current);
        if ((read == ReadResult::Missing) != (saved == checkpoint.Records.end()) || read == ReadResult::Failed ||
            (read == ReadResult::Found && current.Value != saved->Value))
            return Fail("Checkpoint content or adapter policy differs from the current world");
    }
    const uint64_t epoch = m_revision + 1;
    const Vector<uint8_t> marker{'T', 'C', 'W', 'E', 1};
    const auto markerDigest = Digest("$world-epoch", epoch, marker);
    if (!Execute("BEGIN IMMEDIATE"))
        return false;
    struct Rollback { sqlite3* Db; bool Done{}; ~Rollback() { if (!Done) sqlite3_exec(Db, "ROLLBACK", nullptr, nullptr, nullptr); } } rollback{m_db};
    if (!Execute("DELETE FROM state WHERE key!='$world-epoch' AND key!='$restored-checkpoint' AND key!='$restore-pending' AND key NOT GLOB '$checkpoint-reservation/*' AND key NOT GLOB '$participant/*'"))
        return false;
    Statement copy(m_db, "INSERT INTO state SELECT key,value,digest,revision FROM checkpoint_state WHERE checkpoint_id=?1");
    Statement writeEpoch(m_db, "INSERT INTO state VALUES('$world-epoch',?1,?2,?3) ON CONFLICT(key) DO UPDATE SET value=excluded.value,digest=excluded.digest,revision=excluded.revision");
    Statement head(m_db, "UPDATE identity SET revision=?1 WHERE id=1");
    Vector<uint8_t> restored;
    for (size_t byte = 0; byte < 8; ++byte)
        restored.push_back(static_cast<uint8_t>(aId >> (byte * 8)));
    const auto restoredDigest = Digest("$restored-checkpoint", epoch, restored);
    Statement writeRestored(m_db, "INSERT INTO state VALUES('$restored-checkpoint',?1,?2,?3) ON CONFLICT(key) DO UPDATE SET value=excluded.value,digest=excluded.digest,revision=excluded.revision");
    const Vector<uint8_t> pending{1};
    const auto pendingDigest = Digest("$restore-pending", epoch, pending);
    Statement writePending(m_db, "INSERT INTO state VALUES('$restore-pending',?1,?2,?3) ON CONFLICT(key) DO UPDATE SET value=excluded.value,digest=excluded.digest,revision=excluded.revision");
    if (!copy || sqlite3_bind_int64(copy.Value, 1, aId) != SQLITE_OK || sqlite3_step(copy.Value) != SQLITE_DONE ||
        !writeEpoch || sqlite3_bind_blob(writeEpoch.Value, 1, marker.data(), static_cast<int>(marker.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(writeEpoch.Value, 2, markerDigest.data(), 32, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_bind_int64(writeEpoch.Value, 3, epoch) != SQLITE_OK ||
        sqlite3_step(writeEpoch.Value) != SQLITE_DONE || !writeRestored ||
        sqlite3_bind_blob(writeRestored.Value, 1, restored.data(), static_cast<int>(restored.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(writeRestored.Value, 2, restoredDigest.data(), 32, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(writeRestored.Value, 3, epoch) != SQLITE_OK || sqlite3_step(writeRestored.Value) != SQLITE_DONE ||
        !writePending || sqlite3_bind_blob(writePending.Value, 1, pending.data(), 1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(writePending.Value, 2, pendingDigest.data(), 32, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(writePending.Value, 3, epoch) != SQLITE_OK || sqlite3_step(writePending.Value) != SQLITE_DONE ||
        !head || sqlite3_bind_int64(head.Value, 1, epoch) != SQLITE_OK ||
        sqlite3_step(head.Value) != SQLITE_DONE || sqlite3_changes(m_db) != 1 || !Execute("COMMIT"))
        return Fail("Cannot atomically restore checkpoint and advance the world epoch");
    rollback.Done = true;
    m_revision = aNewEpoch = epoch;
    return true;
}

bool WorldStore::LastRestoredCheckpoint(uint64_t& aId, uint64_t& aEpoch) noexcept
{
    aId = aEpoch = 0;
    Record record;
    const auto result = Read("$restored-checkpoint", record);
    if (result == ReadResult::Missing)
        return true;
    if (result != ReadResult::Found || record.Value.size() != 8)
        return Fail("Cannot read the durable rollback admission marker");
    for (size_t byte = 0; byte < 8; ++byte)
        aId |= uint64_t(record.Value[byte]) << (byte * 8);
    aEpoch = record.Revision;
    return (aId != 0 && aId < aEpoch) || Fail("Invalid rollback checkpoint identity");
}

bool WorldStore::IsRestorePending(bool& aPending) noexcept
{
    aPending = false;
    Record pending;
    const auto read = Read("$restore-pending", pending);
    if (read == ReadResult::Missing)
    {
        uint64_t id{}, epoch{};
        return LastRestoredCheckpoint(id, epoch) && id == 0;
    }
    if (read != ReadResult::Found || pending.Value.size() != 1 || pending.Value[0] > 1)
        return Fail("Invalid durable checkpoint rendezvous marker");
    aPending = pending.Value[0] != 0;
    return true;
}
bool WorldStore::CompleteRestore() noexcept
{
    bool pending{};
    if (!IsRestorePending(pending))
        return false;
    if (!pending)
        return true;
    Record record;
    if (Read("$restore-pending", record) != ReadResult::Found)
        return false;
    record.Value = {0};
    uint64_t revision{};
    return Commit({record}, revision) == WriteResult::Committed;
}
