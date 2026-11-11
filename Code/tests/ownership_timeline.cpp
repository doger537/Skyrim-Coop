#include <catch2/catch.hpp>
#include <Structs/OwnershipTimeline.h>
#include "../client/Systems/MovementPlayback.h"

TEST_CASE("Full ownership transfer survives an earlier delta", "[ownership.timeline]")
{
    using OwnershipTimeline::ShouldApplyTransfer;
    REQUIRE(ShouldApplyTransfer(7, 7, 8));
    REQUIRE(ShouldApplyTransfer(8, 7, 8));       // Delta revealed epoch 8 first.
    REQUIRE_FALSE(ShouldApplyTransfer(8, 8, 8)); // Snapshot already applied.
    REQUIRE_FALSE(ShouldApplyTransfer(9, 7, 8)); // Snapshot is genuinely stale.
    REQUIRE_FALSE(ShouldApplyTransfer(0, 0, 0));
    REQUIRE(ShouldApplyTransfer(0, 0, 1));
    REQUIRE(ShouldApplyTransfer(8, 7, 10));
    REQUIRE_FALSE(ShouldApplyTransfer(8, 10, 9));
}

TEST_CASE("Position playback respects separate location coordinate systems", "[playback.location]")
{
    using MovementPlayback::IsLocationDiscontinuity;
    REQUIRE_FALSE(IsLocationDiscontinuity(1, 100, 1, 101)); // Exterior cell boundary.
    REQUIRE_FALSE(IsLocationDiscontinuity(0, 100, 0, 100));
    REQUIRE(IsLocationDiscontinuity(0, 100, 0, 101)); // Different interiors, nearby coordinates.
    REQUIRE(IsLocationDiscontinuity(1, 100, 2, 100));
    REQUIRE(IsLocationDiscontinuity(1, 100, 0, 100));
    REQUIRE(IsLocationDiscontinuity(0, 100, 1, 100));
    const auto before = MovementPlayback::Sample({}, {20.f, 0.f, 0.f}, 100, 200, 150.0, true);
    REQUIRE(before.IsTeleport);
    REQUIRE(before.Position.x == 0.f);
    const auto after = MovementPlayback::Sample({}, {20.f, 0.f, 0.f}, 100, 200, 1000.0, true);
    REQUIRE(after.Position.x == 20.f);
    REQUIRE_FALSE(after.IsExtrapolating);
}
