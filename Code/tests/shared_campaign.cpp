#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <Structs/QuestReplayPlan.h>
#include <Structs/QuestSyncPolicy.h>
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

TEST_CASE("A legacy Helgen dragon scene stays on disk and out of the shared journal", "[quest.migration]")
{
    QuestDirectory directory;
    using Result = Campaign::State::QuestResult;

    Campaign::Quest scene;
    scene.Id = GameId(0, 0x000D0593);
    scene.Type = 1;
    scene.Started = true;
    scene.Stopped = true;
    scene.Events = {{1, 0, true}, {0, 200, true}, {2, 200, true}};

    Campaign::Quest story;
    story.Id = GameId(0, 0x0003372B);
    story.Type = 1;
    story.Started = true;
    story.Events = {{1, 0, true}, {0, 10, true}};
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE_FALSE(state.BootstrapQuests({story, scene}, false));
        REQUIRE_FALSE(state.IsBootstrapped());
        REQUIRE(state.Quests().empty());
        REQUIRE(state.BootstrapQuests({story}, false));
    }

    WorldStore::Record legacy;
    legacy.Key = "quest/skyrim.esm/853395";
    REQUIRE(Campaign::Encode(scene, legacy.Value));
    uint64_t legacyRevision{};
    {
        WorldStore raw;
        REQUIRE(raw.Open(directory.File));
        REQUIRE(raw.Commit({legacy}, legacyRevision) == WorldStore::WriteResult::Committed);
    }
    {
        Campaign::State restored;
        REQUIRE(restored.Open(directory.File));
        REQUIRE(restored.IsBootstrapped());
        REQUIRE(restored.QuestRevision() == legacyRevision);
        REQUIRE(restored.Quests().size() == 1);
        REQUIRE(restored.Quests().front().Data.Id == story.Id);
        REQUIRE(restored.Quests().front().Data.Events == story.Events);
        REQUIRE(restored.FindQuest(scene.Id) == nullptr);

        REQUIRE(restored.PreviewQuest(scene.Id, 1, 0, 10, 1, true) == Result::Ignored);
        REQUIRE(restored.UpdateQuest(scene.Id, 1, 0, 10, 1, true) == Result::Ignored);
        REQUIRE(restored.Revision() == legacyRevision);
        REQUIRE(restored.UpdateQuest(story.Id, 1, 0, 20, 1, false) == Result::Applied);
        REQUIRE(restored.QuestRevision() > legacyRevision);
    }

    WorldStore raw;
    REQUIRE(raw.Open(directory.File));
    WorldStore::Record persisted;
    REQUIRE(raw.Read(legacy.Key, persisted) == WorldStore::ReadResult::Found);
    REQUIRE(persisted.Revision == legacyRevision);
    REQUIRE(persisted.Value == legacy.Value);
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

TEST_CASE("Two post-Helgen characters with separate offer histories can enter and rejoin the same campaign", "[quest.admission]")
{
    using namespace QuestSyncPolicy;
    using Result = Campaign::State::QuestResult;
    QuestDirectory directory;
    const GameId prologue(0, kHelgenPrologue), hadvar(0, kHadvarRoute), scene(0, kHelgenDragonAttack);
    const GameId hostOffer(0, 0x00025231), guestOffer(0, 0x0001CEEE);
    const QuestJournal completed{prologue, 1, 1, true, true,
        {{1, 0, true}, {0, 8, true}, {0, 318, true}, {0, 900, true}, {0, 1000, true}, {2, 1000, true}}};
    const QuestJournal route{hadvar, 1, 1, true, false, {{1, 0, true}, {0, 5, true}, {0, 10, true}, {0, 20, true}}};
    const QuestJournal prepared{hostOffer, 1, 5, true, false, {{1, 0, true}, {0, 0, true}, {0, 1, true}}};

    const auto check = [&](const Campaign::State& acState)
    {
        REQUIRE(acState.IsBootstrapped());
        REQUIRE(acState.Quests().size() == 2);
        QuestReplayPlan plan;
        REQUIRE(PlanQuestReplay(acState.FindQuest(prologue)->Data, {}, false, {true, true, {8, 318, 900, 1000}}, plan));
        REQUIRE(PlanQuestReplay(acState.FindQuest(hadvar)->Data, {}, false, {true, false, {5, 10, 20}}, plan));
        REQUIRE(PlanQuestReplay(acState.FindQuest(prologue)->Data, {}, false, {true, true, {8, 317, 318, 900, 1000}}, plan));
        REQUIRE(PlanQuestReplay(acState.FindQuest(hadvar)->Data, {}, false, {true, false, {3, 5, 10, 20}}, plan));
        REQUIRE(plan.Apply.empty());

        REQUIRE_FALSE(NeedsSharedJournal(guestOffer, 5, {true, false, {0, 1}}));
        REQUIRE_FALSE(NeedsSharedJournal(scene, 1, {true, false, {15, 110, 195}}));
        REQUIRE(NeedsSharedJournal(guestOffer, 5, {true, false, {0, 1, 10}}));
        REQUIRE_FALSE(PlanQuestReplay(acState.FindQuest(prologue)->Data, {}, false, {true, true, {9, 317, 900, 1000}}, plan));
        REQUIRE_FALSE(PlanQuestReplay(acState.FindQuest(hadvar)->Data, {0, 10, 20}, true, {true, false, {3, 5}}, plan));
        REQUIRE(plan.Apply.empty());
    };

    uint64_t revision{};
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE_FALSE(state.IsBootstrapped());
        REQUIRE(state.BootstrapQuests({completed, route, prepared}, false));
        REQUIRE_FALSE(state.BootstrapQuests({}, false));
        revision = state.QuestRevision();
        REQUIRE(state.UpdateQuest(scene, 1, 0, 195, 1, true) == Result::Ignored);
        REQUIRE(state.UpdateQuest(hostOffer, 1, 1, 0, 5, false) == Result::Ignored);
        REQUIRE(state.UpdateQuest(hostOffer, 1, 0, 1, 5, false) == Result::Ignored);
        REQUIRE(state.UpdateQuest(hostOffer, 1, 2, 1, 5, false) == Result::Ignored);
        REQUIRE(state.QuestRevision() == revision);
        check(state);
    }
    Campaign::State rejoined;
    REQUIRE(rejoined.Open(directory.File));
    REQUIRE(rejoined.QuestRevision() == revision);
    REQUIRE_FALSE(rejoined.BootstrapQuests({}, false));
    check(rejoined);
}

TEST_CASE("A legacy radiant offer remains private and its first real stage replaces it atomically", "[quest.admission][quest.migration]")
{
    using Result = Campaign::State::QuestResult;
    QuestDirectory directory;
    const GameId offer(0, 0x00025231); // CR04
    const QuestJournal prepared{offer, 1, 5, true, false, {{1, 0, true}, {0, 0, true}, {0, 1, true}}};
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", QuestContent()));
        REQUIRE(state.BootstrapQuests({}, false));
    }
    WorldStore::Record legacy;
    legacy.Key = "quest/skyrim.esm/152113";
    REQUIRE(Campaign::Encode(prepared, legacy.Value));
    uint64_t revision{};
    {
        WorldStore raw;
        REQUIRE(raw.Open(directory.File));
        REQUIRE(raw.Commit({legacy}, revision) == WorldStore::WriteResult::Committed);
    }
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.IsBootstrapped());
        REQUIRE(state.FindQuest(offer) == nullptr);
        REQUIRE(state.QuestRevision() == revision);
        REQUIRE(state.PreviewQuest(offer, 1, 0, 10, 5, false) == Result::Applied);
        REQUIRE(state.Revision() == revision);
        REQUIRE(state.FindQuest(offer) == nullptr);
        REQUIRE(state.UpdateQuest(offer, 1, 0, 10, 5, false) == Result::Applied);
        REQUIRE(state.UpdateQuest(offer, 1, 0, 10, 5, false) == Result::Duplicate);
        const auto& accepted = state.FindQuest(offer)->Data;
        REQUIRE(accepted.Started);
        REQUIRE(accepted.Events == TiltedPhoques::Vector<QuestJournal::Event>{{0, 10, false}});
        QuestReplayPlan plan;
        REQUIRE_FALSE(PlanQuestReplay(accepted, {0, 1, 10}, true, {true, false, {0, 1}}, plan));
        REQUIRE_FALSE(PlanQuestReplay(accepted, {0, 1, 10}, true, {true, false, {0, 1, 20}}, plan));
        REQUIRE(PlanQuestReplay(accepted, {}, false, {true, false, {0, 1, 10}}, plan));
        REQUIRE(plan.Apply.empty());

        REQUIRE(state.UpdateQuest(offer, 1, 2, 0, 5, false) == Result::Applied);
        REQUIRE(state.UpdateQuest(offer, 2, 1, 0, 5, false) == Result::Ignored);
        REQUIRE(state.UpdateQuest(offer, 2, 0, 1, 5, false) == Result::Ignored);
        REQUIRE(state.FindQuest(offer)->Data.Epoch == 1);
        REQUIRE(state.UpdateQuest(offer, 2, 0, 10, 5, false) == Result::Applied);
        REQUIRE(state.UpdateQuest(offer, 1, 0, 20, 5, false) == Result::Conflict);
        revision = state.QuestRevision();
    }
    Campaign::State restarted;
    REQUIRE(restarted.Open(directory.File));
    REQUIRE(restarted.QuestRevision() == revision);
    REQUIRE(restarted.FindQuest(offer)->Data.Epoch == 2);
    REQUIRE(restarted.FindQuest(offer)->Data.Started);
    REQUIRE(restarted.FindQuest(offer)->Data.Events == TiltedPhoques::Vector<QuestJournal::Event>{{0, 10, false}});
}

TEST_CASE("Admission cannot replay a missing committed story decision", "[quest.admission]")
{
    const QuestJournal story{GameId(0, 0x0004E50D), 1, 1, true, false,
        {{1, 0, false}, {0, 20, false}}, {{"side", 20}}};
    QuestReplayPlan plan;
    REQUIRE_FALSE(PlanQuestReplay(story, {0, 20}, true, {true, false, {0}}, plan));
    REQUIRE(plan.Apply.empty());
    REQUIRE(PlanQuestReplay(story, {}, false, {true, false, {0, 20}}, plan));
}
