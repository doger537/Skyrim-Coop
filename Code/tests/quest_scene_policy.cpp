#include <catch2/catch.hpp>
#include <Structs/QuestApplyConfirmation.h>
#include <Structs/QuestScenePolicy.h>
#include <array>
#include <limits>

using namespace QuestScenePolicy;

TEST_CASE("Scene completion requires the entire ordered prefix for AND conditions", "[quest.scene]")
{
    const std::array conditions{
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 20},
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 30},
    };
    const std::array<uint16_t, 3> stages{20, 20, 30};
    REQUIRE(ClosingPrefix(conditions, 10, std::span(stages).first(1)) == 0);
    REQUIRE(ClosingPrefix(conditions, 10, std::span(stages).first(2)) == 0);
    REQUIRE(ClosingPrefix(conditions, 10, stages) == 3);

    auto alreadyReached = conditions;
    alreadyReached[0].WasDone = true;
    REQUIRE(ClosingPrefix(alreadyReached, 20, std::span(stages).last(1)) == 1);
    alreadyReached[1].WasDone = true;
    REQUIRE(ClosingPrefix(alreadyReached, 30, stages) == 0);
}

TEST_CASE("Scene completion respects OR groups and conservatively handles unobserved conditions", "[quest.scene]")
{
    std::array conditions{
        Condition{Operand::Unknown, Comparison::Equal, 0.f, 0, false, true},
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 20},
        Condition{Operand::CurrentStage, Comparison::GreaterOrEqual, 30.f},
    };
    const std::array<uint16_t, 3> stages{15, 20, 30};
    REQUIRE(ClosingPrefix(conditions, 10, stages) == 3);
    REQUIRE(ClosingPrefix(conditions, 10, std::array<uint16_t, 1>{30}) == 0);

    conditions[0].OrWithNext = false;
    REQUIRE(ClosingPrefix(conditions, 10, stages) == 0);
    conditions[0].OrWithNext = true;
    conditions[2].OrWithNext = true; // Missing last member of this group.
    REQUIRE(ClosingPrefix(conditions, 10, stages) == 0);
}

TEST_CASE("A condition unlocked by one stage may be invalidated by the next stage", "[quest.scene]")
{
    const std::array conditions{
        Condition{Operand::CurrentStage, Comparison::Equal, 20.f},
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 30},
    };
    const std::array<uint16_t, 2> stages{20, 30};
    REQUIRE(ClosingPrefix(conditions, 10, stages) == 0);
    REQUIRE(ClosingPrefix(std::span(conditions).first(1), 10, stages) == 1);
    REQUIRE(ClosingPrefix(std::span(conditions).first(1), 30, std::array<uint16_t, 1>{20}) == 0);
}

TEST_CASE("Unsupported phase expressions cannot authorize a speculative release", "[quest.scene]")
{
    const std::array<uint16_t, 1> stages{20};
    const std::array invalidComparison{Condition{Operand::CurrentStage, static_cast<Comparison>(255), 20.f}};
    REQUIRE(ClosingPrefix(invalidComparison, 10, stages) == 0);
    const std::array nonFinite{Condition{Operand::CurrentStage, Comparison::NotEqual, std::numeric_limits<float>::quiet_NaN()}};
    REQUIRE(ClosingPrefix(nonFinite, 10, stages) == 0);
    std::array<Condition, kMaxConditions + 1> oversized{};
    REQUIRE(ClosingPrefix(oversized, 10, stages) == 0);
    REQUIRE(ClosingPrefix({}, 10, stages) == 0);
}

TEST_CASE("Delayed stage delivery opens the phase without consuming the engine readback budget", "[quest.scene]")
{
    const std::array conditions{
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 20},
        Condition{Operand::CompletedStage, Comparison::Equal, 1.f, 30},
    };
    const std::array<uint16_t, 2> stages{20, 30};
    QuestApplyConfirmation verification;
    using Next = QuestApplyConfirmation::Next;
    REQUIRE(verification.Observe(100, false, ClosingPrefix(conditions, 10, std::span(stages).first(1)) == 0) == Next::Wait);
    REQUIRE(verification.Observe(6500, false, ClosingPrefix(conditions, 10, std::span(stages).first(1)) == 0) == Next::Wait);
    REQUIRE(verification.Deadline == 0);
    REQUIRE(verification.Observe(6600, false, ClosingPrefix(conditions, 10, stages) == 0) == Next::Invoke);
    REQUIRE(verification.Observe(6670, false) == Next::Wait);
    REQUIRE(verification.Observe(6680, true) == Next::Confirm);
}
