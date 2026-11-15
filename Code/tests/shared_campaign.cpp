#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <sqlite3.h>

namespace
{
Mods QuestContent()
{
    Mods mods;
    mods.ModList = {{"Skyrim.esm", 0, false}, {"ccBGSSSE037-Curios.esl", 0, true}};
    mods.ModList[1].Masters = {"Skyrim.esm"};
    mods.Resources = {{"runtime/address-library"}, {"data/loose-files"}, {"data/ccbgssse037-curios.bsa"}};
    for (auto& mod : mods.ModList)
        mod.Digest.fill(42);
    for (auto& resource : mods.Resources)
        resource.Digest.fill(43);
    return mods;
}

TiltedPhoques::String AdapterFile()
{
    // Entirely synthetic policy data. This is not evidence that Rare Curios has this quest.
    std::string digest;
    for (size_t i = 0; i < 32; ++i)
        digest += "2a";
    return TiltedPhoques::String(
        (R"({"schema":1,"adapters":[{"plugin":"ccbgssse037-curios.esl","sha256":")" + digest +
         R"(","quest":"800","stages":[0,10,20,30],"replay_stages":[10],"decisions":[{"id":"side","stages":[20,30]}],"allow_misc":true,"allow_restart":false,"replay_lifecycle":false,"game_verified":true,"evidence":"synthetic unit fixture only"}]})")
            .c_str());
}

struct QuestDirectory
{
    std::filesystem::path Directory = std::filesystem::current_path() / "shared-quest-test";
    std::filesystem::path File = Directory / "world.sqlite3";
    QuestDirectory()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Directory, error));
    }
    ~QuestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }
};
} // namespace

TEST_CASE("Quest adapters require exact content and explicit replay rules", "[quest.adapters]")
{
    Campaign::QuestAdapters adapters;
    const auto json = AdapterFile();
    REQUIRE(adapters.Load(std::string_view(json.data(), json.size())));
    REQUIRE(adapters.Validate(QuestContent()));
    const auto* adapter = adapters.Find("CCBGSSSE037-CURIOS.ESL", 0x800);
    REQUIRE(adapter != nullptr);
    REQUIRE(adapter->ReplayStages == TiltedPhoques::Vector<uint16_t>{10});
    REQUIRE_FALSE(adapter->AllowRestart);
    auto changed = QuestContent();
    changed.ModList.back().Digest[0] ^= 1;
    REQUIRE_FALSE(adapters.Validate(changed));
    REQUIRE_FALSE(adapters.Load("{\"schema\":1,\"adapters\":[{}]}"));
    auto unverified = json;
    const auto position = unverified.find("\"game_verified\":true");
    REQUIRE(position != TiltedPhoques::String::npos);
    unverified.replace(position, 20, "\"game_verified\":false");
    REQUIRE_FALSE(adapters.Load(std::string_view(unverified.data(), unverified.size())));
    REQUIRE_FALSE(adapters.Load("{\"schema\":1,\"adapters\":[]} trailing"));
}

TEST_CASE("Shared quest stages and one story decision survive all players and a server restart", "[quest.durable]")
{
    QuestDirectory directory;
    const GameId id(1, 0x800);
    using Result = Campaign::State::QuestResult;
    uint64_t revision = 0;
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File, AdapterFile()));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE(state.BootstrapQuests({}, false));
        REQUIRE(state.UpdateQuest(id, 1, 1, 0, 2, false) == Result::Applied);
        REQUIRE(state.UpdateQuest(id, 1, 0, 10, 2, false) == Result::Applied);
        REQUIRE(state.UpdateQuest(id, 1, 1, 0, 2, false) == Result::Duplicate);
        REQUIRE(state.UpdateQuest(id, 1, 0, 10, 2, false) == Result::Duplicate);
        REQUIRE(state.UpdateQuest(id, 1, 0, 20, 2, false) == Result::Applied);
        revision = state.QuestRevision();
        REQUIRE(state.UpdateQuest(id, 1, 0, 30, 2, false) == Result::Conflict);
        REQUIRE(state.QuestRevision() == revision);
        REQUIRE(state.FindQuest(id)->Data.Decisions.front().Stage == 20);
    }
    Campaign::State restored;
    REQUIRE(restored.Open(directory.File, AdapterFile()));
    REQUIRE(restored.IsBootstrapped());
    REQUIRE(restored.QuestRevision() == revision);
    REQUIRE(restored.FindQuest(id) != nullptr);
    REQUIRE(restored.FindQuest(id)->Data.Events.size() == 3);
    REQUIRE(restored.UpdateQuest(id, 1, 0, 30, 2, false) == Result::Conflict);
    REQUIRE(restored.UpdateQuest(id, 1, 0, 20, 2, false) == Result::Duplicate);
    REQUIRE(restored.FindQuest(id)->Data.Decisions.front().Stage == 20);
}

TEST_CASE("An old quest run cannot reset a restarted shared quest", "[quest.epoch]")
{
    QuestDirectory directory;
    Campaign::State state;
    REQUIRE(state.Open(directory.File));
    REQUIRE(state.Bind("1.7.104.0", QuestContent()));
    REQUIRE(state.BootstrapQuests({}, false));
    using Result = Campaign::State::QuestResult;
    const GameId id(1, 0x801);
    REQUIRE(state.UpdateQuest(id, 1, 1, 0, 2, false) == Result::Applied);
    REQUIRE(state.UpdateQuest(id, 1, 0, 30, 2, false) == Result::Applied);
    REQUIRE(state.UpdateQuest(id, 1, 2, 30, 2, false) == Result::Applied);
    const auto stopped = state.QuestRevision();
    REQUIRE(state.UpdateQuest(id, 1, 1, 0, 2, false) == Result::Duplicate);
    REQUIRE(state.QuestRevision() == stopped);
    REQUIRE(state.FindQuest(id)->Data.Stopped);
    REQUIRE(state.UpdateQuest(id, 2, 1, 0, 2, false) == Result::Applied);
    REQUIRE(state.UpdateQuest(id, 2, 0, 10, 2, false) == Result::Applied);
    REQUIRE(state.UpdateQuest(id, 1, 0, 99, 2, false) == Result::Conflict);
    REQUIRE(state.FindQuest(id)->Data.Epoch == 2);
    REQUIRE(state.FindQuest(id)->Data.Events.size() == 2);
}

TEST_CASE("A story adapter can forbid quest restart and unknown stages", "[quest.adapters]")
{
    QuestDirectory directory;
    Campaign::State state;
    REQUIRE(state.Open(directory.File, AdapterFile()));
    REQUIRE(state.Bind("1.7.104.0", QuestContent()));
    REQUIRE(state.BootstrapQuests({}, false));
    using Result = Campaign::State::QuestResult;
    const GameId id(1, 0x800);
    REQUIRE(state.UpdateQuest(id, 1, 1, 0, 6, false) == Result::Applied); // explicit misc adapter
    REQUIRE(state.UpdateQuest(id, 1, 0, 999, 6, false) == Result::Conflict);
    REQUIRE(state.UpdateQuest(id, 1, 2, 0, 6, false) == Result::Applied);
    REQUIRE(state.UpdateQuest(id, 2, 1, 0, 6, false) == Result::Conflict);
    REQUIRE(state.UpdateQuest(GameId(1, 0x801), 1, 1, 0, 6, false) == Result::Ignored);
}

TEST_CASE("Campaign baseline is imported atomically once without claiming script order", "[quest.bootstrap]")
{
    QuestDirectory directory;
    Campaign::State state;
    REQUIRE(state.Open(directory.File));
    REQUIRE(state.Bind("1.7.104.0", QuestContent()));
    Campaign::Quest quest;
    quest.Id = GameId(1, 0x800);
    quest.Type = 2;
    quest.Started = true;
    quest.Events = {{1, 0, true}, {0, 30, true}, {0, 20, true}};
    REQUIRE_FALSE(state.BootstrapQuests({quest, quest}, false));
    REQUIRE_FALSE(state.IsBootstrapped());
    REQUIRE(state.Quests().empty());
    REQUIRE(state.BootstrapQuests({quest}, false));
    REQUIRE(state.Quests().front().Data.Events == quest.Events);
    REQUIRE_FALSE(state.BootstrapQuests({}, false));
    REQUIRE(state.UpdateQuest(quest.Id, 1, 0, 30, 2, false) == Campaign::State::QuestResult::Duplicate);
}

TEST_CASE("An empty baseline stays initialized and a different adapter file cannot replace campaign policy", "[quest.bootstrap]")
{
    QuestDirectory directory;
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE(state.BootstrapQuests({}, false));
    }
    Campaign::State restored;
    REQUIRE_FALSE(restored.Open(directory.File, AdapterFile()));
    REQUIRE(restored.Open(directory.File));
    REQUIRE(restored.IsBootstrapped());
    REQUIRE(restored.Quests().empty());
}

TEST_CASE("A failed quest audit write rolls back the snapshot and leaves the cache unchanged", "[quest.durable]")
{
    QuestDirectory directory;
    const GameId id(1, 0x800);
    uint64_t revision = 0;
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE(state.BootstrapQuests({}, false));
        REQUIRE(state.UpdateQuest(id, 1, 1, 0, 2, false) == Campaign::State::QuestResult::Applied);
        revision = state.QuestRevision();
    }
    sqlite3* database = nullptr;
    REQUIRE(sqlite3_open(directory.File.string().c_str(), &database) == SQLITE_OK);
    REQUIRE(
        sqlite3_exec(
            database, "CREATE TRIGGER failed_audit BEFORE INSERT ON state WHEN new.key LIKE 'quest-event/%' BEGIN SELECT RAISE(ABORT,'simulated write failure'); END;", nullptr,
            nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(database) == SQLITE_OK);
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.UpdateQuest(id, 1, 0, 10, 2, false) == Campaign::State::QuestResult::StorageError);
        REQUIRE(state.QuestRevision() == revision);
        REQUIRE(state.FindQuest(id)->Data.Events.size() == 1);
    }
    Campaign::State restored;
    REQUIRE(restored.Open(directory.File));
    REQUIRE(restored.QuestRevision() == revision);
    REQUIRE(restored.FindQuest(id)->Data.Events.size() == 1);
}

TEST_CASE("Legacy object worlds upgrade their campaign contract and old servers cannot decode the new format", "[quest.migration]")
{
    QuestDirectory directory;
    {
        WorldStore legacy;
        REQUIRE(legacy.Open(directory.File));
        Campaign::Contract contract{"1.7.104.0", QuestContent()};
        WorldStore::Record record;
        record.Key = "$contract";
        REQUIRE(Campaign::Encode(contract, record.Value));
        REQUIRE(record.Value.size() >= 7);
        REQUIRE(record.Value[4] == 2); // header version new H decoder refuses
        record.Value[4] = 1;              // H's otherwise identical contract codec
        uint64_t revision = 0;
        REQUIRE(legacy.Commit({record}, revision) == WorldStore::WriteResult::Committed);
    }
    {
        Campaign::State upgraded;
        REQUIRE(upgraded.Open(directory.File));
        REQUIRE(upgraded.GetContract().has_value());
        REQUIRE(upgraded.GetContract()->Content == QuestContent());
        REQUIRE_FALSE(upgraded.IsBootstrapped());
    }
    WorldStore raw;
    REQUIRE(raw.Open(directory.File));
    WorldStore::Record record;
    REQUIRE(raw.Read("$contract", record) == WorldStore::ReadResult::Found);
    REQUIRE(record.Value.size() >= 7);
    REQUIRE(record.Value[4] == 2);
    Campaign::Contract decoded;
    REQUIRE(Campaign::Decode(record.Value, decoded));
    REQUIRE(decoded.Content == QuestContent());
}
