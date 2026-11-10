#include <catch2/catch.hpp>
#include "../client/Systems/MovementPlayback.h"
#include <limits>

using MovementPlayback::Sample;

TEST_CASE("Remote position prediction stays bounded", "[playback.position]")
{
    SECTION("Normal walking remains linear")
    {
        const auto sample = Sample({}, {20.f, 0.f, 0.f}, 100, 200, 150.0);
        REQUIRE(sample.Position.x == Approx(10.f));
        REQUIRE(sample.Delta == Approx(0.5f));
        REQUIRE_FALSE(sample.IsTeleport);
    }
    SECTION("A short tick gap cannot launch a model")
    {
        const auto sample = Sample({}, {0.f, 0.f, 900.f}, 100, 101, 10000.0);
        REQUIRE(sample.Position.z == Approx(1156.f));
        REQUIRE(sample.IsExtrapolating);
    }
    SECTION("Prediction never exceeds one observed displacement")
    {
        REQUIRE(Sample({}, {20.f, 0.f, 0.f}, 100, 130, 10000.0).Position.x == Approx(40.f));
    }
    SECTION("A teleport holds until its tick, then snaps without prediction")
    {
        REQUIRE(Sample({}, {2000.f, 0.f, 0.f}, 100, 200, 150.0).Position.x == 0.f);
        const auto sample = Sample({}, {2000.f, 0.f, 0.f}, 100, 200, 250.0);
        REQUIRE(sample.Position.x == 2000.f);
        REQUIRE(sample.IsTeleport);
        REQUIRE_FALSE(sample.IsExtrapolating);
    }
    SECTION("Reversed and duplicate ticks cannot wrap unsigned subtraction")
    {
        REQUIRE(Sample({}, {50.f, 0.f, 0.f}, 200, 100, 1000.0).Position.x == 50.f);
        REQUIRE(Sample({}, {50.f, 0.f, 0.f}, 100, 100, 1000.0).Position.x == 50.f);
    }
    SECTION("Nonfinite input has a finite fallback")
    {
        const float bad = std::numeric_limits<float>::quiet_NaN();
        REQUIRE_FALSE(MovementPlayback::IsFinite({bad, 0.f, 0.f}));
        REQUIRE(MovementPlayback::IsFinite(Sample({}, {bad, 0.f, 0.f}, 100, 200, 150.0).Position));
        REQUIRE(MovementPlayback::IsFinite(Sample({}, {}, 100, 200, std::numeric_limits<double>::infinity()).Position));
    }
    SECTION("Loss and bursty delivery cannot grow prediction without bound")
    {
        bool bounded = true;
        for (uint64_t gap = 1; gap <= 300; ++gap)
            for (double delay : {0.0, 10.0, 100.0, 500.0, 10000.0})
            {
                const auto sample = Sample({}, {100.f, -300.f, 800.f}, 1000, 1000 + gap, 1000.0 + gap + delay);
                const double dx = sample.Position.x - 100.0;
                const double dy = sample.Position.y + 300.0;
                const double dz = sample.Position.z - 800.0;
                bounded &= std::sqrt(dx * dx + dy * dy + dz * dz) <= 256.001;
            }
        REQUIRE(bounded);
    }
}
