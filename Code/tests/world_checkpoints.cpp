#include <catch2/catch.hpp>
#include <WorldStore.h>
#include <sqlite3.h>

namespace
{
struct CheckpointDirectory
{
    std::filesystem::path Directory = std::filesystem::current_path() / "world-checkpoint-test";
    std::filesystem::path File = Directory / "world.sqlite3";
    CheckpointDirectory() { std::error_code error; REQUIRE(std::filesystem::create_directory(Directory, error)); }
    ~CheckpointDirectory() { std::error_code error; std::filesystem::remove_all(Directory, error); }
    void Execute(const char* apSql)
    {
        sqlite3* db{};
        REQUIRE(sqlite3_open(File.string().c_str(), &db) == SQLITE_OK);
        const int result = sqlite3_exec(db, apSql, nullptr, nullptr, nullptr);
        sqlite3_close(db);
        REQUIRE(result == SQLITE_OK);
    }
};
}

TEST_CASE("A world checkpoint restores the complete old timeline while epochs and reserved IDs only advance", "[world.checkpoint]")
{
    CheckpointDirectory paths;
    WorldStore store;
    REQUIRE(store.Open(paths.File));
    const auto worldId = store.WorldId();
    uint64_t revision{};
    REQUIRE(store.Commit({{"$contract", {9}, 0}, {"$adapters", {8}, 0}, {"object/a/1", {1}, 0},
        {"quest/a/1", {2}, 0}, {"world-item/1", {3}, 0}, {"reward/a/1", {4}, 0}, {"$PARTICIPANT/case-sensitive-story-key", {5}, 0}}, revision) == WorldStore::WriteResult::Committed);
    uint64_t id{};
    REQUIRE(store.ReserveCheckpoint(id));
    REQUIRE(store.CaptureCheckpoint(id, {10, 20}));
    REQUIRE_FALSE(store.CaptureCheckpoint(id, {99})); // Never replace either player's saved-file binding.
    WorldStore::Checkpoint saved;
    REQUIRE(store.ReadCheckpoint(id, saved));
    REQUIRE(saved.Metadata == TiltedPhoques::Vector<uint8_t>{10, 20});
    REQUIRE(saved.Records.size() == 7);
    REQUIRE(store.Commit({{"object/a/1", {99}, 1}, {"quest/a/1", {98}, 1}, {"reward/a/2", {97}, 0},
        {"$participant/registered-after-save", {96}, 0}}, revision) == WorldStore::WriteResult::Committed);
    const auto before = store.Revision();
    uint64_t epoch{};
    REQUIRE(store.RestoreCheckpoint(id, epoch));
    REQUIRE(epoch == before + 1);
    REQUIRE(store.WorldId() == worldId);
    WorldStore::Record record;
    REQUIRE(store.Read("object/a/1", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{1});
    REQUIRE(record.Revision == 1); // Embedded typed record revisions remain consistent.
    REQUIRE(store.Read("reward/a/2", record) == WorldStore::ReadResult::Missing);
    REQUIRE(store.Read("$participant/registered-after-save", record) == WorldStore::ReadResult::Found);
    REQUIRE(store.Read("$world-epoch", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Revision == epoch);
    bool pending{};
    REQUIRE(store.IsRestorePending(pending));
    REQUIRE(pending);
    REQUIRE(store.CompleteRestore());
    REQUIRE(store.IsRestorePending(pending));
    REQUIRE_FALSE(pending);
    uint64_t next{};
    REQUIRE(store.ReserveCheckpoint(next));
    REQUIRE(next > epoch);
    store.Close();
    REQUIRE(store.Open(paths.File));
    REQUIRE(store.VerifyRecords());
    REQUIRE(store.ReadCheckpoint(id, saved));
    REQUIRE(store.RestoreCheckpoint(id, epoch));
    REQUIRE(epoch > next);
}

TEST_CASE("A failed checkpoint restore cannot partially change objects, journal or world epoch", "[world.checkpoint.atomic]")
{
    CheckpointDirectory paths;
    WorldStore store;
    REQUIRE(store.Open(paths.File));
    uint64_t revision{}, id{}, epoch{};
    REQUIRE(store.Commit({{"object/a/1", {1}, 0}, {"quest/a/1", {2}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(store.ReserveCheckpoint(id));
    REQUIRE(store.CaptureCheckpoint(id, {1}));
    REQUIRE(store.Commit({{"object/a/1", {9}, 1}}, revision) == WorldStore::WriteResult::Committed);
    store.Close();
    paths.Execute("CREATE TRIGGER restore_failure BEFORE INSERT ON state WHEN new.key='quest/a/1' BEGIN SELECT RAISE(ABORT,'simulated storage failure'); END;");
    REQUIRE(store.Open(paths.File));
    REQUIRE_FALSE(store.RestoreCheckpoint(id, epoch));
    REQUIRE(epoch == 0);
    REQUIRE(store.Revision() == revision);
    WorldStore::Record record;
    REQUIRE(store.Read("object/a/1", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{9});
    REQUIRE(store.Read("quest/a/1", record) == WorldStore::ReadResult::Found);
    REQUIRE(store.Read("$world-epoch", record) == WorldStore::ReadResult::Missing);
    REQUIRE(store.VerifyRecords());
}

TEST_CASE("Missing or changed checkpoint records are rejected before touching the live timeline", "[world.checkpoint.integrity]")
{
    CheckpointDirectory paths;
    WorldStore store;
    REQUIRE(store.Open(paths.File));
    uint64_t revision{}, id{}, epoch{};
    REQUIRE(store.Commit({{"object/a/1", {1}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(store.ReserveCheckpoint(id));
    REQUIRE(store.CaptureCheckpoint(id, {1}));
    const auto before = store.Revision();
    store.Close();
    paths.Execute("DELETE FROM checkpoint_state WHERE key='object/a/1';");
    REQUIRE(store.Open(paths.File));
    WorldStore::Checkpoint checkpoint;
    REQUIRE_FALSE(store.ReadCheckpoint(id, checkpoint));
    REQUIRE_FALSE(store.RestoreCheckpoint(id, epoch));
    REQUIRE(store.Revision() == before);
    WorldStore::Record record;
    REQUIRE(store.Read("object/a/1", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{1});
}

TEST_CASE("Checkpoint restore cannot downgrade the content agreement", "[world.checkpoint.content]")
{
    CheckpointDirectory paths;
    WorldStore store;
    REQUIRE(store.Open(paths.File));
    uint64_t revision{}, id{}, epoch{};
    REQUIRE(store.Commit({{"$contract", {1}, 0}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE(store.ReserveCheckpoint(id));
    REQUIRE(store.CaptureCheckpoint(id, {1}));
    REQUIRE(store.Commit({{"$contract", {2}, 1}}, revision) == WorldStore::WriteResult::Committed);
    REQUIRE_FALSE(store.RestoreCheckpoint(id, epoch));
    REQUIRE(store.Revision() == revision);
    WorldStore::Record record;
    REQUIRE(store.Read("$contract", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value == TiltedPhoques::Vector<uint8_t>{2});
}
