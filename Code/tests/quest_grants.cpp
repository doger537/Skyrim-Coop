#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <QuestGrant.h>
#include <sqlite3.h>
#include <limits>

namespace
{
Mods Content()
{
    Mods content;
    content.ModList = {{"Skyrim.esm", 0, false}};
    content.Resources = {{"runtime/address-library"}, {"data/loose-files"}};
    content.ModList[0].Digest.fill(42);
    for (auto& resource : content.Resources) resource.Digest.fill(43);
    return content;
}
Campaign::QuestRunParticipants Participants()
{
    Campaign::QuestRunParticipants participants;
    participants.Players[0].Parts[0] = 1;
    participants.Players[1].Parts[0] = 2;
    participants.Initiator = participants.Players[1]; // Guest took this quest.
    return participants;
}
Campaign::QuestGrantPlan Plan()
{
    Campaign::QuestGrantPlan plan;
    plan.Quest = GameId(0, 0x810); plan.Run = 1; plan.Stage = 10;
    plan.Initiator = Participants().Initiator; plan.Event = "stage10.reward.1";
    Campaign::QuestGrantItem key;
    key.Role = Campaign::QuestGrantItem::Key; key.Item.BaseId = GameId(0, 0x811); key.Item.Count = 1; key.Item.IsQuestItem = true;
    Campaign::QuestGrantItem gold;
    gold.Role = Campaign::QuestGrantItem::Reward; gold.Item.BaseId = GameId(0, 0xF); gold.Item.Count = 500;
    Campaign::QuestGrantItem unique;
    unique.Role = Campaign::QuestGrantItem::Reward; unique.Item.BaseId = GameId(0, 0x812); unique.Item.Count = 1;
    unique.Item.ExtraHealth = 1.3f; unique.Item.ExtraCharge = 50.f; unique.Item.ExtraWorn = true;
    unique.Item.ExtraEnchantId = GameId(UINT32_MAX, 0xFF);
    unique.Item.EnchantData.Effects.push_back({3.f, 0, 10, 0.f, GameId(0, 0x813)});
    plan.Items = {key, gold, unique};
    return plan;
}
struct Directory
{
    std::filesystem::path Root = std::filesystem::current_path() / "quest-grant-test";
    std::filesystem::path File = Root / "world.sqlite3";
    Directory() { std::error_code error; REQUIRE(std::filesystem::create_directory(Root, error)); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(Root, error); }
    void Execute(const char* apSql)
    {
        sqlite3* db{};
        REQUIRE(sqlite3_open(File.string().c_str(), &db) == SQLITE_OK);
        const int result = sqlite3_exec(db, apSql, nullptr, nullptr, nullptr);
        sqlite3_close(db); REQUIRE(result == SQLITE_OK);
    }
};
void Initialize(Campaign::State& aWorld, const Directory& acDirectory)
{
    REQUIRE(aWorld.Open(acDirectory.File)); REQUIRE(aWorld.Bind("1.7.104.0", Content()));
    REQUIRE(aWorld.AdvanceWorldEpoch()); REQUIRE(aWorld.BootstrapQuests({}, false));
    const auto participants = Participants();
    REQUIRE(aWorld.UpdateQuest(Plan().Quest, 1, 1, 10, 1, false, &participants) == Campaign::State::QuestResult::Applied);
}
}

TEST_CASE("Quest grant codec rejects truncation and keeps both recipient identities and unique effects", "[world.quest.grant.codec]")
{
    Campaign::QuestGrantReceipt receipt; receipt.Plan = Plan(); receipt.Participants = Participants(); receipt.Revision = 99;
    TiltedPhoques::Vector<uint8_t> bytes;
    REQUIRE(Campaign::Encode(receipt, bytes));
    Campaign::QuestGrantReceipt decoded;
    REQUIRE(Campaign::Decode(bytes, decoded)); REQUIRE(decoded.Plan == receipt.Plan); REQUIRE(decoded.Participants == receipt.Participants);
    REQUIRE(decoded.Revision == 0); // Trusted row revision, never supplied by a payload.
    for (size_t size = 0; size < bytes.size(); ++size)
        REQUIRE_FALSE(Campaign::Decode(TiltedPhoques::Vector<uint8_t>(bytes.begin(), bytes.begin() + size), decoded));
    bytes.push_back(0); REQUIRE_FALSE(Campaign::Decode(bytes, decoded));
    auto invalid = receipt;
    invalid.Plan.Event = "../other"; REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
    invalid = receipt; invalid.Plan.Items[0].Role = Campaign::QuestGrantItem::Reward; REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
    invalid = receipt; invalid.Plan.Items[1].Item.Count = -1; REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
    invalid = receipt; invalid.Plan.Items[1].Item.ExtraHealth = std::numeric_limits<float>::quiet_NaN(); REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
    invalid = receipt; invalid.Participants.Players[0] = invalid.Participants.Players[1]; REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
}

TEST_CASE("Only the quest initiator receives the key and both pinned participants receive rewards once", "[world.quest.grant]")
{
    Directory directory;
    const auto participants = Participants(); const auto plan = Plan();
    uint64_t committed{};
    {
        Campaign::State world; Initialize(world, directory);
        Campaign::QuestGrantReceipt receipt;
        REQUIRE(world.CommitQuestGrant(plan, receipt) == Campaign::State::GrantResult::Committed);
        committed = receipt.Revision; REQUIRE(committed == world.Revision());
        const auto guest = receipt.For(participants.Initiator);
        const auto host = receipt.For(participants.Players[0]);
        REQUIRE(guest.Entries.size() == 3); REQUIRE(host.Entries.size() == 2);
        REQUIRE(guest.Entries[0].IsQuestItem); REQUIRE(host.Entries[0].BaseId == GameId(0, 0xF));
        REQUIRE(host.Entries[0].Count == 500); REQUIRE(guest.Entries[1].Count == 500);
        REQUIRE(host.Entries[1].EnchantData == plan.Items[2].Item.EnchantData); REQUIRE_FALSE(host.Entries[1].ExtraWorn);
        CharacterIdentity outsider; outsider.Parts[0] = 3; REQUIRE(receipt.For(outsider).Entries.empty());
        REQUIRE(world.CommitQuestGrant(plan, receipt) == Campaign::State::GrantResult::Duplicate);
        REQUIRE(world.Revision() == committed); REQUIRE(receipt.Revision == committed);
        auto conflicting = plan; conflicting.Items[1].Item.Count = 1000;
        REQUIRE(world.CommitQuestGrant(conflicting, receipt) == Campaign::State::GrantResult::Conflict); REQUIRE(world.Revision() == committed);
        conflicting.Event = "different.event"; conflicting.Initiator = participants.Players[0];
        REQUIRE(world.CommitQuestGrant(conflicting, receipt) == Campaign::State::GrantResult::Invalid);
        conflicting = plan; conflicting.Event = "unaccepted.stage"; conflicting.Stage = 999;
        REQUIRE(world.CommitQuestGrant(conflicting, receipt) == Campaign::State::GrantResult::Invalid);
        TiltedPhoques::Vector<Campaign::QuestGrantReceipt> outbox;
        REQUIRE(world.ReadQuestGrants(participants.Players[0], 0, outbox)); REQUIRE(outbox.size() == 1);
        REQUIRE(world.ReadQuestGrants(participants.Players[1], committed, outbox)); REQUIRE(outbox.empty());
    }
    Campaign::State reopened;
    REQUIRE(reopened.Open(directory.File)); REQUIRE(reopened.Bind("1.7.104.0", Content()));
    TiltedPhoques::Vector<Campaign::QuestGrantReceipt> outbox;
    REQUIRE(reopened.ReadQuestGrants(participants.Players[1], 0, outbox)); REQUIRE(outbox.size() == 1);
    REQUIRE(outbox[0].Plan == plan); REQUIRE(outbox[0].Revision == committed);
}

TEST_CASE("An error writing a reward inbox cannot leave a host-only grant committed", "[world.quest.grant.atomic]")
{
    Directory directory; uint64_t before{};
    { Campaign::State world; Initialize(world, directory); before = world.Revision(); }
    directory.Execute("CREATE TRIGGER fail_inbox BEFORE INSERT ON state WHEN new.key GLOB 'quest-inbox/*' BEGIN SELECT RAISE(ABORT,'simulated inbox failure'); END;");
    {
        Campaign::State world; REQUIRE(world.Open(directory.File)); REQUIRE(world.Bind("1.7.104.0", Content()));
        Campaign::QuestGrantReceipt receipt;
        REQUIRE(world.CommitQuestGrant(Plan(), receipt) == Campaign::State::GrantResult::StorageError); REQUIRE(world.Revision() == before);
        TiltedPhoques::Vector<Campaign::QuestGrantReceipt> outbox;
        REQUIRE(world.ReadQuestGrants(Participants().Players[0], 0, outbox)); REQUIRE(outbox.empty());
        REQUIRE(world.ReadQuestGrants(Participants().Players[1], 0, outbox)); REQUIRE(outbox.empty());
    }
    directory.Execute("DROP TRIGGER fail_inbox;");
    Campaign::State retry; REQUIRE(retry.Open(directory.File)); REQUIRE(retry.Bind("1.7.104.0", Content()));
    Campaign::QuestGrantReceipt receipt; REQUIRE(retry.CommitQuestGrant(Plan(), receipt) == Campaign::State::GrantResult::Committed);
}

TEST_CASE("Shared checkpoint rollback returns the reward ledger and both inboxes to the same point", "[world.quest.grant.rollback]")
{
    Directory directory; Campaign::State world; Initialize(world, directory);
    Campaign::QuestGrantReceipt receipt;
    REQUIRE(world.CommitQuestGrant(Plan(), receipt) == Campaign::State::GrantResult::Committed);
    const uint64_t original = receipt.Revision;
    uint64_t checkpoint{}; REQUIRE(world.ReserveCheckpoint(checkpoint)); REQUIRE(world.CaptureCheckpoint(checkpoint, {1}));
    auto next = Plan(); next.Event = "stage10.reward.2";
    REQUIRE(world.CommitQuestGrant(next, receipt) == Campaign::State::GrantResult::Committed);
    const uint64_t future = receipt.Revision;
    REQUIRE(world.RestoreCheckpoint(checkpoint));
    TiltedPhoques::Vector<Campaign::QuestGrantReceipt> outbox;
    for (const auto& character : Participants().Players)
    {
        REQUIRE(world.ReadQuestGrants(character, 0, outbox)); REQUIRE(outbox.size() == 1); REQUIRE(outbox[0].Revision == original);
    }
    REQUIRE(world.CommitQuestGrant(next, receipt) == Campaign::State::GrantResult::Committed); REQUIRE(receipt.Revision > future);
}

TEST_CASE("Reward outbox pages are bounded and continue without skipping recipients", "[world.quest.grant.pagination]")
{
    Directory directory; Campaign::State world; Initialize(world, directory);
    auto plan = Plan(); Campaign::QuestGrantReceipt receipt;
    for (size_t i = 0; i < 70; ++i)
    {
        plan.Event = TiltedPhoques::String("reward.") + std::to_string(i).c_str();
        REQUIRE(world.CommitQuestGrant(plan, receipt) == Campaign::State::GrantResult::Committed);
    }
    TiltedPhoques::Vector<Campaign::QuestGrantReceipt> page;
    REQUIRE(world.ReadQuestGrants(Participants().Initiator, 0, page)); REQUIRE(page.size() == 64);
    for (size_t i = 1; i < page.size(); ++i) REQUIRE(page[i].Revision > page[i - 1].Revision);
    const uint64_t cursor = page.back().Revision;
    REQUIRE(world.ReadQuestGrants(Participants().Initiator, cursor, page)); REQUIRE(page.size() == 6); REQUIRE(page.front().Revision > cursor);
    REQUIRE_FALSE(world.ReadQuestGrants(Participants().Initiator, world.Revision() + 1, page)); REQUIRE(page.empty());
}

TEST_CASE("Deleting one reward recipient index makes world opening fail closed", "[world.quest.grant.integrity]")
{
    Directory directory;
    {
        Campaign::State world; Initialize(world, directory);
        Campaign::QuestGrantReceipt receipt; REQUIRE(world.CommitQuestGrant(Plan(), receipt) == Campaign::State::GrantResult::Committed);
    }
    directory.Execute("DELETE FROM state WHERE key=(SELECT key FROM state WHERE key GLOB 'quest-inbox/*' LIMIT 1);");
    Campaign::State broken; REQUIRE_FALSE(broken.Open(directory.File)); REQUIRE_FALSE(broken.IsReady());
}
