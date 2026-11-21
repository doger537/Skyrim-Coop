#include <catch2/catch.hpp>
#include <CampaignState.h>
#include <limits>

namespace
{
WorldItemState SavedItem()
{
    WorldItemState item;
    item.Id = 7;
    item.WorldEpoch = 2;
    item.HostEpoch = 3;
    item.Revision = 7;
    item.Sequence = 1;
    item.Tick = 100;
    item.CellId = GameId(0, 0x801);
    item.WorldSpaceId = GameId(0, 0x3c);
    item.Item.BaseId = GameId(0, 0x802);
    item.Item.Count = 3;
    item.Item.ExtraCharge = 23.25f;
    item.Item.ExtraEnchantId = GameId(UINT32_MAX, 25);
    item.Item.EnchantData.IsWeapon = true;
    item.Item.EnchantData.Effects.push_back({3.f, 4, 5, 6.f, GameId(0, 0x803)});
    item.Position = glm::vec3(1.125f, -2.25f, 3.5f);
    item.Rotation = {0.70710678f, 0.f, 0.70710678f, 0.f};
    item.LinearVelocity = {10.f, 20.f, -1.f};
    item.AngularVelocity = {0.5f, -0.25f, 1.f};
    return item;
}

Mods SavedContent()
{
    Mods content;
    content.ModList = {{"Skyrim.esm", 0, false}};
    content.ModList.front().Digest.fill(42);
    content.Resources = {{"runtime/address-library"}, {"data/loose-files"}};
    for (auto& resource : content.Resources)
        resource.Digest.fill(43);
    return content;
}

struct ItemDirectory
{
    std::filesystem::path Path = std::filesystem::current_path() / "world-item-state-test";
    std::filesystem::path Database = Path / "world.sqlite3";
    ItemDirectory()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Path, error));
    }
    ~ItemDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Path, error);
    }
};
}

TEST_CASE("Dropped-object persistence retains physical pose and custom effects without transient handles", "[world-item.persistence]")
{
    const auto original = SavedItem();
    TiltedPhoques::Vector<uint8_t> bytes;
    REQUIRE(Campaign::Encode(original, bytes));
    WorldItemState decoded;
    REQUIRE(Campaign::Decode(bytes, decoded));
    REQUIRE(decoded == original);
    for (size_t length = 0; length < bytes.size(); ++length)
    {
        TiltedPhoques::Vector<uint8_t> partial(bytes.begin(), bytes.begin() + length);
        WorldItemState untouched = original;
        REQUIRE_FALSE(Campaign::Decode(partial, untouched));
        REQUIRE(untouched == original);
    }
    bytes.push_back(0);
    REQUIRE_FALSE(Campaign::Decode(bytes, decoded));
    Campaign::Contract contract{"1.7.104.0", SavedContent()};
    REQUIRE(Campaign::Encode(contract, bytes));
    REQUIRE_FALSE(Campaign::Decode(bytes, decoded));
    auto invalid = original;
    invalid.Item.EnchantData.Effects.resize(WorldItemState::kMaxEffects + 1);
    REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
    invalid = original;
    invalid.LinearVelocity[0] = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(Campaign::Encode(invalid, bytes));
}

TEST_CASE("Dropped-object identities and tombstones survive restart and cannot be reused", "[world-item.persistence]")
{
    ItemDirectory directory;
    WorldItemState item = SavedItem();
    uint64_t id{};
    uint64_t epoch{};
    {
        Campaign::State state;
        REQUIRE(state.Open(directory.Database));
        REQUIRE(state.Bind("1.7.104.0", SavedContent()));
        REQUIRE(state.AdvanceWorldEpoch());
        item.WorldEpoch = state.WorldEpoch();
        epoch = item.WorldEpoch;
        item.Id = 0;
        REQUIRE(state.SaveWorldItem(item, 0) == Campaign::State::ItemWriteResult::Saved);
        id = item.Id;
        REQUIRE(id != 0);
        const uint64_t beforeDelete = item.Revision;
        item.Deleted = true;
        ++item.Sequence;
        ++item.Tick;
        REQUIRE(state.SaveWorldItem(item, beforeDelete) == Campaign::State::ItemWriteResult::Saved);
        REQUIRE(item.Revision > beforeDelete);
    }
    Campaign::State reopened;
    REQUIRE(reopened.Open(directory.Database));
    REQUIRE(reopened.WorldEpoch() == epoch);
    REQUIRE(reopened.AdvanceWorldEpoch());
    TiltedPhoques::Vector<WorldItemState> all;
    REQUIRE(reopened.LoadWorldItems(all));
    REQUIRE(all.size() == 1);
    REQUIRE(all.front() == item);
    const uint64_t retiredRevision = item.Revision;
    item.WorldEpoch = reopened.WorldEpoch();
    item.Deleted = false;
    ++item.Sequence;
    REQUIRE(reopened.SaveWorldItem(item, retiredRevision) == Campaign::State::ItemWriteResult::Conflict);
    item.Id = 0;
    REQUIRE(reopened.SaveWorldItem(item, 0) == Campaign::State::ItemWriteResult::Saved);
    REQUIRE(item.Id > id);
    REQUIRE(reopened.LoadWorldItems(all));
    REQUIRE(all.size() == 2);
}

TEST_CASE("Dropped-object storage rejects stale updates and unloaded content without committing", "[world-item.persistence]")
{
    ItemDirectory directory;
    Campaign::State state;
    REQUIRE(state.Open(directory.Database));
    REQUIRE(state.Bind("1.7.104.0", SavedContent()));
    REQUIRE(state.AdvanceWorldEpoch());
    auto item = SavedItem();
    item.Id = 0;
    item.WorldEpoch = state.WorldEpoch();
    REQUIRE(state.SaveWorldItem(item, 0) == Campaign::State::ItemWriteResult::Saved);
    const auto first = item;
    ++item.Sequence;
    item.Position.x = 17.25f;
    REQUIRE(state.SaveWorldItem(item, first.Revision) == Campaign::State::ItemWriteResult::Saved);
    const uint64_t committed = state.Revision();
    auto stale = first;
    stale.Position.x = -500.f;
    REQUIRE(state.SaveWorldItem(stale, first.Revision) == Campaign::State::ItemWriteResult::Conflict);
    // Ordinary motion updates cannot replace the persisted item payload.
    auto changed = item;
    ++changed.Sequence;
    changed.Item.BaseId.ModId = 99;
    REQUIRE(state.SaveWorldItem(changed, item.Revision) == Campaign::State::ItemWriteResult::Conflict);
    auto invalid = item;
    ++invalid.Sequence;
    invalid.CellId.ModId = 99;
    REQUIRE(state.SaveWorldItem(invalid, item.Revision) == Campaign::State::ItemWriteResult::Invalid);
    // A new item still validates its base form against the content contract.
    invalid = item;
    invalid.Id = 0;
    invalid.Item.BaseId.ModId = 99;
    REQUIRE(state.SaveWorldItem(invalid, 0) == Campaign::State::ItemWriteResult::Invalid);
    invalid = item;
    ++invalid.Sequence;
    ++invalid.WorldEpoch;
    REQUIRE(state.SaveWorldItem(invalid, item.Revision) == Campaign::State::ItemWriteResult::Invalid);
    invalid = item;
    invalid.Id = committed + 100;
    REQUIRE(state.SaveWorldItem(invalid, 0) == Campaign::State::ItemWriteResult::Conflict);
    REQUIRE(state.Revision() == committed);
    TiltedPhoques::Vector<WorldItemState> saved;
    REQUIRE(state.LoadWorldItems(saved));
    REQUIRE(saved.size() == 1);
    REQUIRE(saved.front() == item);
}
