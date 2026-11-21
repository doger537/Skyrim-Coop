#include <catch2/catch.hpp>
#include <Structs/QuestReplayPlan.h>
#include <Structs/QuestApplyConfirmation.h>
#include <Structs/QuestSyncPolicy.h>

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

TEST_CASE("A local-only Companions offer is eligible for admission until its first objective", "[quest.replay]")
{
    const GameId offer(0, 0x0001CEEE); // CR01
    LocalQuestState local{true, false, {0, 1}};
    REQUIRE_FALSE(NeedsSharedJournal(offer, 5, local));
    REQUIRE(NeedsSharedJournal(GameId(1, offer.BaseId), 5, local));
    REQUIRE(NeedsSharedJournal(GameId(0, 0x0004B2D9), 5, local));
    local.Reached.push_back(10);
    REQUIRE(NeedsSharedJournal(offer, 5, local));
}

TEST_CASE("Completed Helgen histories can differ on audited stages without replay", "[quest.replay]")
{
    QuestJournal journal;
    journal.Id = GameId(0, QuestSyncPolicy::kHelgenPrologue);
    journal.Type = 1;
    journal.Started = true;
    journal.Stopped = true;
    journal.Events = {{1, 0, true}, {0, 318, true}, {0, 900, true}, {0, 1000, true}, {2, 1000, true}};
    LocalQuestState local{true, true, {317, 900, 1000}};
    QuestReplayPlan plan;

    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());

    SECTION("A player still in Helgen must finish")
    {
        local.Stopped = false;
        local.Reached = {317, 900};
    }
    SECTION("An incomplete imported run stays strict")
    {
        journal.Events[3].Stage = 800;
    }
    SECTION("The completed tutorial must agree on the chosen guide")
    {
        journal.Events.push_back({0, 8, true}); // Hadvar
        local.Reached.push_back(9); // Ralof
    }
    SECTION("Unaudited prologue progress remains significant after escape")
    {
        local.Reached.push_back(1001);
    }
    SECTION("A later run stays strict")
    {
        journal.Epoch = 2;
    }
    SECTION("A live stage stays strict")
    {
        journal.Events[1].Bootstrap = false;
    }
    SECTION("An explicit shared decision stays strict")
    {
        journal.Decisions.push_back({"route", 318});
    }
    SECTION("A form from a different plugin stays strict")
    {
        journal.Id.ModId = 1;
    }
    SECTION("A different Skyrim quest stays strict")
    {
        journal.Id.BaseId = 0x0004E50D;
    }
    REQUIRE_FALSE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
}

TEST_CASE("The Hadvar route accepts an imported difference in Alduin cleanup", "[quest.replay]")
{
    QuestJournal journal;
    journal.Id = GameId(0, QuestSyncPolicy::kHadvarRoute);
    journal.Type = 1;
    journal.Started = true;
    journal.Events = {{1, 0, true}, {0, 5, true}, {0, 10, true}, {0, 20, true}};
    LocalQuestState local{true, false, {3, 5, 10, 20}};
    QuestReplayPlan plan;

    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
    journal.Events.push_back({0, 3, true});
    local.Reached.erase(local.Reached.begin());
    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());

    SECTION("A missing conversation stage still blocks admission")
    {
        local.Reached.pop_back();
    }
    SECTION("An extra story stage still blocks admission")
    {
        local.Reached.push_back(30);
    }
    SECTION("The two players must have reached the Riverwood scene")
    {
        journal.Events.erase(journal.Events.begin() + 2);
        local.Reached.erase(local.Reached.begin() + 1);
    }
    SECTION("A live Alduin stage stays in the shared history")
    {
        journal.Events.back().Bootstrap = false;
    }
    SECTION("A different quest cannot use the exception")
    {
        journal.Id.BaseId = 0x0004E50D;
    }
    REQUIRE_FALSE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
}

TEST_CASE("An unaccepted Companions radiant offer never needs lifecycle replay", "[quest.replay]")
{
    QuestJournal journal;
    journal.Id = GameId(0, 0x00025231); // CR04
    journal.Type = 5;
    journal.Started = true;
    journal.Events = {{1, 0, true}, {0, 0, true}, {0, 1, true}};
    LocalQuestState local{true, true, {}};
    QuestReplayPlan plan;

    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
    local.Stopped = false;
    local.Reached = {0};
    REQUIRE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());

    SECTION("A player has accepted the offer")
    {
        local.Reached.push_back(10);
    }
    SECTION("The shared quest already has a real objective")
    {
        journal.Events.push_back({0, 10, true});
    }
    SECTION("The offer was modified during the campaign")
    {
        journal.Events.back().Bootstrap = false;
    }
    SECTION("The main Companions quest keeps its own checks")
    {
        journal.Id.BaseId = 0x0004B2D9; // C00
    }
    SECTION("A form from another plugin cannot use the rule")
    {
        journal.Id.ModId = 1;
    }
    REQUIRE_FALSE(PlanQuestReplay(journal, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());
}

TEST_CASE("Diagnostic-only history comparison shows the Hadvar cleanup without approving replay", "[quest.admission]")
{
    const QuestJournal route{GameId(0, QuestSyncPolicy::kHadvarRoute), 1, 1, true, false,
        {{1, 0, true}, {0, 5, true}, {0, 10, true}, {0, 20, true}}};
    const LocalQuestState local{true, false, {3, 5, 10, 20}};
    QuestReplayPlan plan;
    REQUIRE_FALSE(PlanQuestReplay(route, {}, false, local, plan, false));
    REQUIRE(plan.Problem == QuestAdmissionProblem::LocalProgress);
    REQUIRE(plan.Apply.empty());
    REQUIRE(PlanQuestReplay(route, {}, false, local, plan));
    REQUIRE(plan.Apply.empty());

    const QuestJournal ralof{GameId(0, QuestSyncPolicy::kRalofRoute), 1, 1, true, false, route.Events};
    REQUIRE(PlanQuestReplay(ralof, {}, false, local, plan));
    const LocalQuestState notYetOnRoute{true, false, {3, 5, 20}};
    REQUIRE_FALSE(PlanQuestReplay(ralof, {}, false, notYetOnRoute, plan));
}

TEST_CASE("An incoming quest update waits for native readback and never retries a fragment", "[quest.admission]")
{
    using Next = QuestApplyConfirmation::Next;
    QuestApplyConfirmation verification;
    REQUIRE(verification.Observe(10, false) == Next::Invoke);
    // ScriptSetStage returned false and the engine has not reached this stage.
    REQUIRE(verification.Observe(11, false) == Next::Wait);
    REQUIRE(verification.Observe(5009, false) == Next::Wait);
    REQUIRE(verification.Observe(5010, false) == Next::Failed);
    // After reconnect a fresh observation confirms an already applied update
    // without calling its script again.
    verification = {};
    REQUIRE(verification.Observe(5020, true) == Next::Confirm);
    REQUIRE(verification.Deadline == 0);

    REQUIRE(verification.Observe(50, false) == Next::Invoke);
    REQUIRE(verification.Observe(51, false) == Next::Wait);
    REQUIRE(verification.Observe(52, true) == Next::Confirm);
}

TEST_CASE("A Creation quest with a displayed objective at stage zero is not a Companions offer", "[quest.admission]")
{
    const GameId bittercup(1, 0xD63);
    const LocalQuestState local{true, false, {0}};
    REQUIRE(NeedsSharedJournal(bittercup, 8, local));
    const QuestJournal gameQuest{bittercup, 1, 8, true, false, {{1, 0, true}, {0, 0, true}}};
    QuestReplayPlan plan;
    REQUIRE_FALSE(PlanQuestReplay(gameQuest, {}, false, {true, true, {}}, plan));
    REQUIRE(plan.Apply.empty());
}
