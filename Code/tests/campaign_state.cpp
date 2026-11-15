#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <Structs/ContentCompatibility.h>
#include <limits>

namespace
{
Mods Content()
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

ObjectData Container()
{
    ObjectData object;
    object.Id = GameId(1, 0x800);
    object.CellId = GameId(1, 0x801);
    object.ServerId = 12345;
    object.IsSenderFirst = true;
    object.CurrentLockData.IsLocked = true;
    object.CurrentLockData.LockLevel = 49;
    Inventory::Entry item;
    item.BaseId = GameId(1, 0x802);
    item.Count = 2;
    item.ExtraHealth = 1.2f;
    item.ExtraCharge = 23.f;
    item.ExtraEnchantId = GameId(UINT32_MAX, 25);
    item.EnchantData.IsWeapon = true;
    item.EnchantData.Effects.push_back({3.f, 4, 5, 6.f, GameId(0, 0x803)});
    item.ExtraPoisonId = GameId(0, 0x804);
    item.ExtraPoisonCount = 1;
    item.IsQuestItem = true;
    object.CurrentInventory.Entries.push_back(item);
    return object;
}

struct CampaignDirectory
{
    std::filesystem::path Directory = std::filesystem::current_path() / "campaign-state-test";
    std::filesystem::path File = Directory / "world.sqlite3";
    CampaignDirectory()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Directory, error));
    }
    ~CampaignDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }
};
} // namespace

TEST_CASE("Persistent object codec preserves item effects and excludes transient entity IDs", "[world.codec]")
{
    const auto object = Container();
    TiltedPhoques::Vector<uint8_t> bytes;
    REQUIRE(Campaign::Encode(object, bytes));
    ObjectData decoded;
    REQUIRE(Campaign::Decode(bytes, decoded));
    REQUIRE(decoded.ServerId == 0);
    REQUIRE_FALSE(decoded.IsSenderFirst);
    REQUIRE(decoded.Id == object.Id);
    REQUIRE(decoded.CurrentInventory == object.CurrentInventory);
    REQUIRE(decoded.CurrentInventory.Entries.front().EnchantData == object.CurrentInventory.Entries.front().EnchantData);
    REQUIRE(decoded.CurrentLockData == object.CurrentLockData);
    for (size_t size = 0; size < bytes.size(); ++size)
    {
        TiltedPhoques::Vector<uint8_t> truncated(bytes.begin(), bytes.begin() + size);
        REQUIRE_FALSE(Campaign::Decode(truncated, decoded));
    }
    bytes.push_back(0);
    REQUIRE_FALSE(Campaign::Decode(bytes, decoded));
}

TEST_CASE("Persistent codecs reject another record kind, nonfinite data and oversized counts", "[world.codec]")
{
    auto object = Container();
    TiltedPhoques::Vector<uint8_t> bytes;
    object.CurrentInventory.Entries.front().ExtraCharge = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(Campaign::Encode(object, bytes));
    object = Container();
    object.CurrentInventory.Entries.resize(4097);
    REQUIRE_FALSE(Campaign::Encode(object, bytes));
    Campaign::Contract contract{"1.7.104.0", Content()};
    REQUIRE(Campaign::Encode(contract, bytes));
    REQUIRE_FALSE(Campaign::Decode(bytes, object));
    Campaign::Contract decoded;
    REQUIRE(Campaign::Decode(bytes, decoded));
    REQUIRE(decoded.Content == contract.Content);
    bytes[7] = 0xff; // runtime string length, not an allocation instruction
    REQUIRE_FALSE(Campaign::Decode(bytes, decoded));
}

TEST_CASE("Saved content contract and CC container survive a new campaign instance", "[world.campaign]")
{
    CampaignDirectory directory;
    TiltedPhoques::String worldId;
    const auto object = Container();
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.File));
        REQUIRE(state.Bind("1.7.104.0", Content()));
        worldId = state.WorldId();
        REQUIRE(state.SaveObject(object));
    }
    Campaign::State restored;
    REQUIRE(restored.Open(directory.File));
    REQUIRE(restored.WorldId() == worldId);
    REQUIRE(restored.GetContract().has_value());
    ObjectData decoded;
    bool found = false;
    REQUIRE(restored.LoadObject(object.Id, decoded, found));
    REQUIRE(found);
    REQUIRE(decoded.CurrentInventory == object.CurrentInventory);
    REQUIRE(decoded.ServerId == 0);
    auto changed = Content();
    changed.Resources.back().Digest[0] ^= 1;
    REQUIRE_FALSE(restored.Bind("1.7.104.0", changed));
    REQUIRE_FALSE(restored.Bind("1.6.1170.0", Content()));
    REQUIRE(restored.Bind("1.7.104.0", Content()));
}

TEST_CASE("Persistent forms reject missing plugins and invalid ESL base IDs", "[world.campaign]")
{
    CampaignDirectory directory;
    Campaign::State state;
    REQUIRE(state.Open(directory.File));
    REQUIRE(state.Bind("1.7.104.0", Content()));
    auto object = Container();
    object.Id.BaseId = 0x1000;
    REQUIRE_FALSE(state.SaveObject(object));
    object = Container();
    object.CurrentInventory.Entries.front().BaseId.ModId = 99;
    REQUIRE_FALSE(state.SaveObject(object));
    object = Container();
    object.CurrentInventory.Entries.front().Count = -1;
    REQUIRE_FALSE(state.SaveObject(object));
    object = Container();
    object.CurrentInventory.Entries.front().EnchantData.Effects.clear();
    REQUIRE_FALSE(state.SaveObject(object));
}

TEST_CASE("Inventory arithmetic and enchantment identity preserve stack contents", "[world.inventory]")
{
    auto inventory = Container().CurrentInventory;
    auto delta = inventory.Entries.front();
    delta.Count = -3;
    REQUIRE_FALSE(Campaign::ApplyInventoryDelta(inventory, delta));
    REQUIRE(inventory.Entries.front().Count == 2);
    delta.Count = 1;
    delta.EnchantData.Effects.front().Magnitude += 1;
    REQUIRE(Campaign::ApplyInventoryDelta(inventory, delta));
    REQUIRE(inventory.Entries.size() == 2);
    delta = inventory.Entries.front();
    delta.Count = std::numeric_limits<int32_t>::max();
    REQUIRE_FALSE(Campaign::ApplyInventoryDelta(inventory, delta));
    delta.Count = -2;
    REQUIRE(Campaign::ApplyInventoryDelta(inventory, delta));
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries.front().Count == 1);
}

TEST_CASE("Quest state codec preserves branch order and story decisions", "[world.codec]")
{
    Campaign::Quest quest;
    quest.Id = GameId(1, 0x800);
    quest.Epoch = 2;
    quest.Started = true;
    quest.Type = 2;
    quest.Events = {{1, 0}, {0, 30}, {0, 20}};
    quest.Decisions = {{"branch", 30}};
    TiltedPhoques::Vector<uint8_t> bytes;
    REQUIRE(Campaign::Encode(quest, bytes));
    Campaign::Quest decoded;
    REQUIRE(Campaign::Decode(bytes, decoded));
    REQUIRE(decoded.Events == quest.Events);
    REQUIRE(decoded.Decisions == quest.Decisions);
    REQUIRE(decoded.Epoch == 2);
}
