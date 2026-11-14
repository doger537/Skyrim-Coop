#include <catch2/catch.hpp>
#include <Structs/QuestReplayPlan.h>

TEST_CASE("Quest replay keeps committed event order and requires an explicit stage allowlist", "[quest.replay]")
{
    QuestJournal journal;
    journal.Id = GameId(1, 0x800);
    journal.Type = 2;
    journal.Started = true;
    journal.Events = {{1, 0, false}, {0, 30, false}, {0, 20, false}};
    LocalQuestState local{true, false, {0}};
    QuestReplayPlan plan;
    REQUIRE(PlanQuestReplay(journal, {20, 30}, false, local, plan));
    REQUIRE(plan.Apply.size() == 2);
    REQUIRE(plan.Apply[0].Stage == 30);
    REQUIRE(plan.Apply[1].Stage == 20);
    REQUIRE_FALSE(PlanQuestReplay(journal, {30}, false, local, plan));
    REQUIRE(plan.Apply.empty());
    local.Reached = {0, 20, 30};
    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
    local.Reached.push_back(40);
    REQUIRE_FALSE(PlanQuestReplay(journal, {20, 30}, false, local, plan));
}

TEST_CASE("A completed-stage import is never treated as safe script replay order", "[quest.replay]")
{
    QuestJournal journal;
    journal.Type = 2;
    journal.Started = true;
    journal.Events = {{1, 0, true}, {0, 30, true}, {0, 20, true}};
    LocalQuestState local{true, false, {0}};
    QuestReplayPlan plan;
    REQUIRE_FALSE(PlanQuestReplay(journal, {0, 20, 30}, true, local, plan));
    REQUIRE(plan.Apply.empty());
    local.Reached = {0, 20, 30};
    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
}

TEST_CASE("Lifecycle replay is explicit and cannot silently reset another quest run", "[quest.replay]")
{
    QuestJournal journal;
    journal.Type = 2;
    journal.Started = true;
    journal.Stopped = true;
    journal.Events = {{1, 0, false}, {0, 10, false}, {2, 10, false}};
    LocalQuestState local{true, true, {}};
    QuestReplayPlan plan;
    REQUIRE_FALSE(PlanQuestReplay(journal, {0, 10}, false, local, plan));
    REQUIRE(PlanQuestReplay(journal, {0, 10}, true, local, plan));
    REQUIRE(plan.Apply.size() == 3);
    REQUIRE(plan.Apply.front().Status == 1);
    REQUIRE(plan.Apply.back().Status == 2);
    journal.Epoch = 2;
    REQUIRE_FALSE(PlanQuestReplay(journal, {0, 10}, true, local, plan));
    REQUIRE(plan.Apply.empty());
}
