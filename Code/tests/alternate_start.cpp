#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <utility>
#include <catch2/catch.hpp>
#include <Structs/ContentCompatibility.h>
#include <Structs/AlternateStartPolicy.h>

TEST_CASE("Alternate Start requires its dependency before accepting a profile", "[content.alternate-start]")
{
    Mods mods;
    mods.ModList.push_back({"Skyrim.esm", 0, false});
    mods.ModList.push_back({"Alternate Start - Live Another Life.esp", 3, false});
    for (auto& entry : mods.ModList)
        entry.Digest.fill(42);
    REQUIRE(ContentCompatibility::Validate(mods).Reason == ContentCompatibility::Issue::MissingDependency);
    mods.ModList.push_back({"Unofficial Skyrim Special Edition Patch.esp", 4, false});
    mods.ModList.back().Digest.fill(43);
    REQUIRE(ContentCompatibility::Validate(mods).Reason == ContentCompatibility::Issue::DifferentOrder);
    std::swap(mods.ModList[1], mods.ModList[2]);
    REQUIRE(static_cast<bool>(ContentCompatibility::Validate(mods)));
}

TEST_CASE("Personal start quest IDs follow the loaded plugin slot", "[content.alternate-start]")
{
    REQUIRE(AlternateStartPolicy::OwnsForm(0x05001234, 5, false));
    REQUIRE_FALSE(AlternateStartPolicy::OwnsForm(0x0003372B, 5, false));
    REQUIRE_FALSE(AlternateStartPolicy::OwnsForm(0x06001234, 5, false));
    REQUIRE(AlternateStartPolicy::OwnsForm(0xFE123800, 0x123, true));
    REQUIRE_FALSE(AlternateStartPolicy::OwnsForm(0xFE124800, 0x123, true));
    REQUIRE_FALSE(AlternateStartPolicy::OwnsForm(0xFF001234, 0xFF, false));
}
