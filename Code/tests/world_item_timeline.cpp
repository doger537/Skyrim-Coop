#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <WorldItemTimeline.h>
#include <cmath>
#include <type_traits>

namespace
{
WorldItemState Spawn()
{
    WorldItemState state;
    state.Id = 7;
    state.WorldEpoch = 10;
    state.HostEpoch = 20;
    state.Revision = 1;
    state.Sequence = 1;
    state.Tick = 1000;
    state.CellId = GameId(0, 0x978);
    state.Item.BaseId = GameId(0, 0xf);
    state.Item.Count = 1;
    return state;
}
using Result = WorldItemTimeline::Result;
static_assert(std::is_trivially_copyable_v<WorldItemMotion>);
}

TEST_CASE("Motion cannot spawn or resurrect a dropped instance", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState state = Spawn();
    REQUIRE(timeline.Motion(state) == Result::Unknown);
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
    REQUIRE(timeline.Lifecycle(state) == Result::Duplicate);
    state.Deleted = true;
    ++state.Revision;
    ++state.Sequence;
    REQUIRE(timeline.Lifecycle(state) == Result::Removed);
    WorldItemMotion sample;
    REQUIRE_FALSE(timeline.Sample(state.Id, state.Tick, sample));
    state.Deleted = false;
    ++state.Revision;
    ++state.Sequence;
    REQUIRE(timeline.Motion(state) == Result::Stale);
    REQUIRE(timeline.Lifecycle(state) == Result::Stale);
    timeline.Reset(11, 20);
    REQUIRE(timeline.Lifecycle(state) == Result::Stale);
    state.WorldEpoch = 11;
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
}

TEST_CASE("A tombstone arriving before spawn still prevents late creation", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState removed = Spawn();
    removed.Revision = 2;
    removed.Deleted = true;
    REQUIRE(timeline.Lifecycle(removed) == Result::Removed);
    REQUIRE(timeline.Lifecycle(Spawn()) == Result::Stale);
    REQUIRE(timeline.Latest(removed.Id) == nullptr);
}

TEST_CASE("World item motion rejects stale sequences, revisions, host leases and clocks", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState state = Spawn();
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
    WorldItemMotion update = state;
    ++update.Sequence;
    update.Tick += 50;
    update.Position.x = 10.f;
    REQUIRE(timeline.Motion(update) == Result::Applied);
    REQUIRE(timeline.Motion(update) == Result::Stale);
    ++update.Sequence;
    SECTION("revision") { ++update.Revision; }
    SECTION("world epoch") { ++update.WorldEpoch; }
    SECTION("host epoch") { ++update.HostEpoch; }
    SECTION("clock regression") { update.Tick = 999; }
    REQUIRE(timeline.Motion(update) == Result::Stale);
    REQUIRE(timeline.Latest(state.Id)->Position.x == 10.f);
}

TEST_CASE("Item playback interpolates the shortest quaternion arc and bounds prediction", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState state = Spawn();
    state.LinearVelocity[0] = 20.f;
    const float angle = 179.f * 3.14159265359f / 180.f / 2.f;
    state.Rotation = {std::cos(angle), 0.f, 0.f, std::sin(angle)};
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
    WorldItemMotion update = state;
    update.Sequence = 2;
    update.Tick = 1100;
    update.Position.x = 2.f;
    update.Rotation[3] = -update.Rotation[3];
    REQUIRE(timeline.Motion(update) == Result::Applied);
    WorldItemMotion sample;
    REQUIRE(timeline.Sample(state.Id, 1050, sample));
    REQUIRE(sample.Position.x == Approx(1.f));
    REQUIRE(std::abs(sample.Rotation[0]) < 0.02f);
    REQUIRE(std::abs(sample.Rotation[3]) > 0.99f);
    REQUIRE(timeline.Sample(state.Id, 100000, sample));
    REQUIRE(sample.Position.x == Approx(4.f));
    update.Mode = WorldItemMotion::Motion::Sleeping;
    update.Sequence++;
    update.Tick = 1200;
    REQUIRE(timeline.Motion(update) == Result::Applied);
    REQUIRE(timeline.Sample(state.Id, 100000, sample));
    REQUIRE(sample.Position.x == Approx(2.f));
}

TEST_CASE("A cell change or teleport replaces interpolation history", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState state = Spawn();
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
    WorldItemMotion update = state;
    update.Sequence = 2;
    update.Tick = 1100;
    SECTION("cell") { update.CellId = GameId(0, 0x979); update.Position.x = 2.f; }
    SECTION("teleport") { update.Position.x = 10000.f; }
    REQUIRE(timeline.Motion(update) == Result::Applied);
    WorldItemMotion sample;
    REQUIRE(timeline.Sample(state.Id, 1050, sample));
    REQUIRE(sample.Position.x == update.Position.x);
    REQUIRE(sample.CellId == update.CellId);
}

TEST_CASE("Item history remains bounded after its ring wraps", "[world-item.timeline]")
{
    WorldItemTimeline timeline;
    timeline.Reset(10, 20);
    WorldItemState state = Spawn();
    REQUIRE(timeline.Lifecycle(state) == Result::Created);
    for (uint64_t sequence = 2; sequence <= 20; ++sequence)
    {
        WorldItemMotion update = state;
        update.Sequence = sequence;
        update.Tick += sequence * 50;
        update.Position.x = static_cast<float>(sequence);
        REQUIRE(timeline.Motion(update) == Result::Applied);
    }
    WorldItemMotion sample;
    REQUIRE(timeline.Sample(state.Id, 0, sample));
    REQUIRE(sample.Sequence == 5);
    REQUIRE(sample.Position.x == 5.f);
}
