#include <catch2/catch.hpp>
#include <WorldStore.h>
#include <sqlite3.h>
#include <fstream>

namespace
{
struct StoreDirectory
{
    std::filesystem::path Directory = std::filesystem::current_path() / "world-store-test";
    std::filesystem::path File = Directory / "world.sqlite3";
    StoreDirectory()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Directory, error));
    }
    ~StoreDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }
    void Execute(const char* apSql)
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(File.string().c_str(), &db) == SQLITE_OK);
        const int result = sqlite3_exec(db, apSql, nullptr, nullptr, nullptr);
        sqlite3_close(db);
        REQUIRE(result == SQLITE_OK);
    }
};
} // namespace

TEST_CASE("A bounded revision page never silently splits one committed transaction", "[world.store.pagination]")
{
    StoreDirectory directory; WorldStore store; REQUIRE(store.Open(directory.File));
    uint64_t revision{};
    REQUIRE(store.Commit({{"event/a", {1}, 0}, {"event/b", {2}, 0}, {"event/c", {3}, 0}}, revision) == WorldStore::WriteResult::Committed);
    TiltedPhoques::Vector<WorldStore::Record> page;
    REQUIRE_FALSE(store.ReadPrefixAfterRevision("event/", 0, 2, page)); REQUIRE(page.empty());
    const uint64_t first = revision;
    REQUIRE(store.Commit({{"event/d", {4}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(store.ReadPrefixAfterRevision("event/", 0, 3, page)); REQUIRE(page.size() == 3);
    REQUIRE(store.ReadPrefixAfterRevision("event/", first, 3, page)); REQUIRE(page.size() == 1); REQUIRE(page[0].Key == "event/d");
    REQUIRE_FALSE(store.ReadPrefixAfterRevision("event/", revision + 1, 3, page)); REQUIRE(page.empty());
    REQUIRE_FALSE(store.ReadPrefixAfterRevision("event/", 0, 0, page)); REQUIRE_FALSE(store.ReadPrefixAfterRevision("event", 0, 3, page));
}

TEST_CASE("World records survive closing and reopening with a stable world identity", "[world.store]")
{
    StoreDirectory directory;
    WorldStore store;
    REQUIRE(store.Open(directory.File));
    const auto id = store.WorldId();
    REQUIRE(id.size() == 32);
    uint64_t revision = 0;
    REQUIRE(store.Commit({{"object/cc-example.esl/2048", {1, 2, 3}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(revision == 1);
    store.Close();
    REQUIRE(store.Open(directory.File));
    REQUIRE(store.WorldId() == id);
    REQUIRE(store.Revision() == 1);
    WorldStore::Record record;
    REQUIRE(store.Read("object/cc-example.esl/2048", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{1, 2, 3});
    REQUIRE(record.Revision == 1);
}

TEST_CASE("Schema two upgrades preserve world records and fence servers without quest grants", "[world.store]")
{
    StoreDirectory directory;
    WorldStore store;
    REQUIRE(store.Open(directory.File));
    const auto identity = store.WorldId();
    uint64_t revision{};
    REQUIRE(store.Commit({{"quest/migration/1", {7, 17}, 0}}, revision) == WorldStore::WriteResult::Committed);
    store.Close();
    directory.Execute("PRAGMA user_version=2;");
    REQUIRE(store.Open(directory.File));
    REQUIRE(store.WorldId() == identity);
    REQUIRE(store.Revision() == revision);
    WorldStore::Record record;
    REQUIRE(store.Read("quest/migration/1", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{7, 17});
    store.Close();
    sqlite3* db{};
    REQUIRE(sqlite3_open(directory.File.string().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* schema{};
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &schema, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(schema) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(schema, 0) == 3);
    sqlite3_finalize(schema);
    sqlite3_close(db);
}

TEST_CASE("A stale revision cannot overwrite a newer object and a failed batch rolls back", "[world.store]")
{
    StoreDirectory directory;
    WorldStore store;
    REQUIRE(store.Open(directory.File));
    uint64_t revision = 0;
    REQUIRE(store.Commit({{"object/a/1", {10}, 0}, {"object/a/2", {20}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(store.Commit({{"object/a/1", {11}, 1}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(revision == 2);
    REQUIRE(store.Commit({{"object/a/2", {21}, 1}, {"object/a/1", {99}, 1}}, revision) == WorldStore::WriteResult::Conflict);
    WorldStore::Record record;
    REQUIRE(store.Read("object/a/2", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value.front() == 20);
    REQUIRE(store.Revision() == 2);
    store.Close();
    directory.Execute("CREATE TRIGGER disk_failure BEFORE INSERT ON state WHEN new.key='object/fail/1' BEGIN SELECT RAISE(ABORT,'simulated disk write failure'); END;");
    REQUIRE(store.Open(directory.File));
    REQUIRE(store.Commit({{"object/a/2", {22}, 1}, {"object/fail/1", {99}, 0}}, revision) == WorldStore::WriteResult::Failed);
    REQUIRE(store.Read("object/a/2", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value.front() == 20);
    REQUIRE(store.Read("object/fail/1", record) == WorldStore::ReadResult::Missing);
    REQUIRE(store.Revision() == 2);
}

TEST_CASE("World payload corruption is refused without recreating the world", "[world.store]")
{
    StoreDirectory directory;
    WorldStore store;
    REQUIRE(store.Open(directory.File));
    const auto id = store.WorldId();
    uint64_t revision = 0;
    REQUIRE(store.Commit({{"quest/example/1", {1, 2, 3}, 0}}, revision) == WorldStore::WriteResult::Committed);
    store.Close();
    directory.Execute("UPDATE state SET value=x'ff' WHERE key='quest/example/1'");
    REQUIRE(store.Open(directory.File));
    REQUIRE(store.WorldId() == id);
    WorldStore::Record record;
    REQUIRE(store.Read("quest/example/1", record) == WorldStore::ReadResult::Failed);
    REQUIRE(store.Error().find("SHA-256") != TiltedPhoques::String::npos);
    TiltedPhoques::Vector<WorldStore::Record> records;
    REQUIRE_FALSE(store.ReadPrefix("quest/", records));
}

TEST_CASE("Only one server can own a world database", "[world.store]")
{
    StoreDirectory directory;
    WorldStore owner;
    WorldStore second;
    REQUIRE(owner.Open(directory.File));
    REQUIRE_FALSE(second.Open(directory.File));
    REQUIRE_FALSE(second.IsOpen());
    owner.Close();
    REQUIRE(second.Open(directory.File));
}

TEST_CASE("World records reject duplicate keys and enumerate a bounded namespace", "[world.store]")
{
    StoreDirectory directory;
    WorldStore store;
    REQUIRE(store.Open(directory.File));
    uint64_t revision = 0;
    REQUIRE(store.Commit({{"object/a/1", {1}, 0}, {"object/a/1", {2}, 0}}, revision) == WorldStore::WriteResult::Failed);
    REQUIRE(store.Commit({{"object/a/1", {}, 0}, {"quest/a/1", {2}, 0}}, revision) == WorldStore::WriteResult::Committed);
    TiltedPhoques::Vector<WorldStore::Record> records;
    REQUIRE(store.ReadPrefix("object/", records));
    REQUIRE(records.size() == 1);
    REQUIRE(records.front().Key == "object/a/1");
    REQUIRE(records.front().Value.empty());
}

TEST_CASE("Unrelated databases and unknown world schemas are preserved and refused", "[world.store]")
{
    StoreDirectory directory;
    directory.Execute("CREATE TABLE user_data(important TEXT); INSERT INTO user_data VALUES('keep me');");
    WorldStore store;
    REQUIRE_FALSE(store.Open(directory.File));
    directory.Execute("DROP TABLE user_data; PRAGMA user_version=99;");
    REQUIRE_FALSE(store.Open(directory.File));
    REQUIRE_FALSE(store.IsOpen());
}
