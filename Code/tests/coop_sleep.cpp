#include <catch2/catch.hpp>
#include "../common/CoopSleepVote.h"

using Phase = CoopSleepVote::Phase;
using Action = CoopSleepVote::Action;

TEST_CASE("Cooperative rest requires both votes and both live UI acknowledgements", "[coop.sleep]")
{
    CoopSleepVote vote;
    vote.Configure(3, 10, 20);
    REQUIRE(vote.Submit(10, 1, 0, Action::Vote, 8, 100, 100));
    REQUIRE(vote.GetPhase() == Phase::Waiting);
    const uint64_t round = vote.Round();
    REQUIRE(vote.Submit(20, 1, 0, Action::Vote, 4, 100, 110)); // simultaneous opening
    REQUIRE(vote.GetPhase() == Phase::Preparing);
    REQUIRE(vote.Hours() == 4);
    REQUIRE(vote.Votes() == 3);
    REQUIRE(vote.Submit(20, 2, round, Action::Ready, 0, 120, 120));
    REQUIRE(vote.GetPhase() == Phase::Preparing);
    REQUIRE(vote.Submit(10, 2, round, Action::Ready, 0, 130, 130));
    REQUIRE(vote.GetPhase() == Phase::Committed);
    REQUIRE_FALSE(vote.Submit(10, 2, round, Action::Ready, 0, 130, 130));
    REQUIRE_FALSE(vote.Submit(10, 3, round, Action::Cancel, 0, 140, 140));
    REQUIRE_FALSE(vote.Expire(999999));
    REQUIRE(vote.GetPhase() == Phase::Committed);
}

TEST_CASE("Disconnected, cancelled, stale and uninvited rest votes cannot advance time", "[coop.sleep]")
{
    CoopSleepVote vote;
    vote.Configure(3, 10, 20);
    REQUIRE_FALSE(vote.Submit(30, 1, 0, Action::Vote, 8, 100, 100));
    REQUIRE_FALSE(vote.Submit(10, 1, 0, Action::Vote, 8, 100, 20000));
    REQUIRE_FALSE(vote.Submit(10, 1, 0, Action::Vote, 8, 3000, 100));
    REQUIRE(vote.Submit(10, 1, 0, Action::Vote, 8, 100, 100));
    const uint64_t round = vote.Round();
    REQUIRE_FALSE(vote.Submit(20, 1, round + 1, Action::Vote, 4, 100, 110));
    REQUIRE_FALSE(vote.Submit(10, 1, round, Action::Vote, 8, 100, 110));
    REQUIRE(vote.Submit(20, 2, round, Action::Cancel, 0, 100, 110));
    REQUIRE(vote.GetPhase() == Phase::Cancelled);
    REQUIRE_FALSE(vote.Submit(10, 2, round, Action::Ready, 0, 100, 110));
    REQUIRE(vote.Submit(10, 3, 0, Action::Vote, 8, 120, 120));
    REQUIRE(vote.Round() > round);
    REQUIRE(vote.Expire(120 + CoopSleepVote::kWindowMs));
    REQUIRE(vote.GetPhase() == Phase::Cancelled);
    REQUIRE_FALSE(vote.Submit(20, 3, round, Action::Ready, 0, 30120, 30120));
    vote.Configure(3, 10, 0);
    REQUIRE_FALSE(vote.Submit(10, 4, 0, Action::Vote, 8, 30200, 30200));
    vote.Configure(4, 10, 20);
    REQUIRE_FALSE(vote.Submit(20, 4, round, Action::Ready, 0, 30200, 30200));
}

TEST_CASE("Cooperative rest has a fixed window that repeated requests cannot extend", "[coop.sleep]")
{
    CoopSleepVote vote;
    vote.Configure(1, 10, 20);
    REQUIRE_FALSE(vote.Submit(10, 1, 0, Action::Vote, 25, 1, 1));
    REQUIRE(vote.Submit(10, 2, 0, Action::Vote, 24, 2, 2));
    const uint64_t deadline = vote.Deadline();
    REQUIRE(vote.Submit(10, 3, vote.Round(), Action::Vote, 12, 20000, 20000));
    REQUIRE(vote.Deadline() == deadline);
    REQUIRE(vote.Submit(20, 1, vote.Round(), Action::Vote, 8, 20001, 20001));
    REQUIRE(vote.GetPhase() == Phase::Preparing);
    REQUIRE(vote.Expire(deadline));
    REQUIRE_FALSE(vote.Submit(10, 4, vote.Round(), Action::Ready, 0, deadline, deadline));
}
