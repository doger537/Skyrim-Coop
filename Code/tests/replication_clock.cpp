#include <catch2/catch.hpp>
#include "../common/ReplicationClock.h"
#include <limits>

TEST_CASE("Connection clock epochs cannot leak into the next session", "[replication.clock]")
{
    ReplicationClock::TickBaseline clock;
    REQUIRE(clock.Sample(9000000) == 0);
    REQUIRE(clock.Sample(9000016) == 16);
    clock.Reset();
    REQUIRE(clock.Sample(200) == 0);
    REQUIRE(clock.Sample(216) == 16);
    clock.Rebase(800000000);
    REQUIRE(clock.Sample(800000016) == 16);
    clock.Rebase(0);
    REQUIRE(clock.Sample(16) == 16);
}

TEST_CASE("Clock regression recovers immediately instead of freezing", "[replication.clock]")
{
    ReplicationClock::TickBaseline clock;
    clock.Rebase(1000);
    REQUIRE(clock.Sample(900) == 0);
    REQUIRE(clock.Sample(916) == 16);
    REQUIRE(clock.Sample(916) == 0);
}

TEST_CASE("Replication cadence bounds traffic without losing fractional intervals", "[replication.cadence]")
{
    ReplicationClock::Cadence cadence(60000);
    REQUIRE_FALSE(cadence.Advance(59999));
    REQUIRE(cadence.Advance(1));
    REQUIRE_FALSE(cadence.Advance(1));
    REQUIRE(cadence.Advance(180004)); // One refresh, never a catch-up burst.
    REQUIRE_FALSE(cadence.Advance(59994));
    REQUIRE(cadence.Advance(1));
    cadence.Reset();
    REQUIRE_FALSE(cadence.Advance(59999));
    REQUIRE(cadence.Advance(std::numeric_limits<uint64_t>::max()));
    ReplicationClock::Cadence disabled(0);
    REQUIRE_FALSE(disabled.Advance(std::numeric_limits<uint64_t>::max()));
}

TEST_CASE("Clock correction takes the shortest path through midnight", "[replication.clock]")
{
    REQUIRE(ReplicationClock::BlendHour(23.f, 1.f, 0.5f) == Approx(0.f));
    REQUIRE(ReplicationClock::BlendHour(1.f, 23.f, 0.5f) == Approx(0.f));
    REQUIRE(ReplicationClock::BlendHour(12.f, 11.f, 0.5f) == Approx(11.5f));
    REQUIRE(ReplicationClock::BlendHour(23.f, 1.f, -1.f) == Approx(23.f));
    REQUIRE(ReplicationClock::BlendHour(23.f, 1.f, 2.f) == Approx(1.f));
}
