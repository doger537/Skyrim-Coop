#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <CorePluginUpgrade.h>

namespace
{
Mods PreviousCore()
{
    Mods mods;
    mods.ModList = {{"Skyrim.esm", 0, false}, {"Update.esm", 1, false}, {"SkyrimTogether.esp", 2, false}};
    mods.ModList[0].Digest.fill(42);
    mods.ModList[1].Digest.fill(43);
    mods.ModList[1].Masters = {"Skyrim.esm"};
    mods.ModList[2].Masters = {"Skyrim.esm", "Update.esm"};
    mods.ModList[2].Digest = CorePluginUpgrade::kPrevious;
    mods.Resources = {{"runtime/address-library"}, {"data/loose-files"}};
    for (auto& resource : mods.Resources)
        resource.Digest.fill(44);
    return mods;
}
struct UpgradeDirectory
{
    std::filesystem::path Directory = std::filesystem::current_path() / "core-upgrade-test";
    std::filesystem::path File = Directory / "world.sqlite3";
    UpgradeDirectory()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Directory, error));
    }
    ~UpgradeDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }
};
}

TEST_CASE("Core marker upgrade changes only the known bundled plugin digest and never downgrades", "[core.upgrade]")
{
    const auto original = PreviousCore();
    auto updated = original;
    REQUIRE(CorePluginUpgrade::Upgrade(updated));
    REQUIRE(updated.ModList[2].Digest == CorePluginUpgrade::kCheckpoints);
    REQUIRE(updated.Resources == original.Resources);
    REQUIRE(updated.ModList[0] == original.ModList[0]);
    REQUIRE(updated.ModList[1] == original.ModList[1]);
    updated.ModList[2].Digest = CorePluginUpgrade::kPrevious;
    REQUIRE(updated == original);
    updated.ModList[2].Digest = CorePluginUpgrade::kMarkers;
    REQUIRE(CorePluginUpgrade::Upgrade(updated));
    REQUIRE(updated.ModList[2].Digest == CorePluginUpgrade::kCheckpoints);
    updated.ModList[2].Digest = CorePluginUpgrade::kCharacters;
    REQUIRE(CorePluginUpgrade::Upgrade(updated));
    REQUIRE(updated.ModList[2].Digest == CorePluginUpgrade::kCheckpoints);
    const auto next = updated;
    REQUIRE_FALSE(CorePluginUpgrade::Upgrade(updated));
    REQUIRE(updated == next);
    updated = original;
    updated.ModList[2].Digest[0] ^= 1;
    const auto unknown = updated;
    REQUIRE_FALSE(CorePluginUpgrade::Upgrade(updated));
    REQUIRE(updated == unknown);
    updated = original;
    updated.ModList[2].Filename = "DifferentMod.esp";
    REQUIRE_FALSE(CorePluginUpgrade::Upgrade(updated));
    updated = original;
    updated.ModList[2].IsLite = true;
    REQUIRE_FALSE(CorePluginUpgrade::Upgrade(updated));
    updated = original;
    updated.ModList[2].Masters.clear();
    REQUIRE_FALSE(CorePluginUpgrade::Upgrade(updated));
}

TEST_CASE("Restoring a previous campaign preserves its objects while committing the additive core upgrade once", "[core.upgrade.persistence]")
{
    UpgradeDirectory paths;
    const auto previous = PreviousCore();
    ObjectData object;
    object.Id = GameId(0, 0x800);
    object.CellId = GameId(0, 0x801);
    object.CurrentLockData.IsLocked = true;
    Inventory::Entry item;
    item.BaseId = GameId(0, 0x802);
    item.Count = 7;
    object.CurrentInventory.Entries.push_back(item);
    uint64_t originalRevision{};
    TiltedPhoques::String worldId;
    {
        Campaign::State oldWorld;
        REQUIRE(oldWorld.Open(paths.File));
        REQUIRE(oldWorld.Bind("1.7.104.0", previous));
        REQUIRE(oldWorld.SaveObject(object));
        originalRevision = oldWorld.Revision();
        worldId = oldWorld.WorldId();
    }
    uint64_t upgradedRevision{};
    {
        Campaign::State current;
        REQUIRE(current.Open(paths.File));
        REQUIRE(current.WorldId() == worldId);
        REQUIRE(current.Revision() == originalRevision + 1);
        auto expected = previous;
        REQUIRE(CorePluginUpgrade::Upgrade(expected));
        REQUIRE(current.GetContract()->Content == expected);
        REQUIRE(current.Bind("1.7.104.0", expected));
        REQUIRE_FALSE(current.Bind("1.7.104.0", previous));
        ObjectData restored;
        bool found{};
        REQUIRE(current.LoadObject(object.Id, restored, found));
        REQUIRE(found);
        REQUIRE(restored.CurrentInventory == object.CurrentInventory);
        REQUIRE(restored.CurrentLockData == object.CurrentLockData);
        upgradedRevision = current.Revision();
    }
    {
        Campaign::State reopened;
        REQUIRE(reopened.Open(paths.File));
        REQUIRE(reopened.WorldId() == worldId);
        REQUIRE(reopened.Revision() == upgradedRevision);
    }
}
