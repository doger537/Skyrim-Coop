#include <catch2/catch.hpp>
#include <Structs/QuestEventHistory.h>

TEST_CASE("Duplicate quest lifecycle events do not reopen already relayed stages", "[quest.history]")
{
    QuestEventHistory history;
    REQUIRE(history.Accept(QuestEventHistory::Started, 0));
    REQUIRE(history.Accept(QuestEventHistory::StageUpdate, 10));
    REQUIRE_FALSE(history.Accept(QuestEventHistory::Started, 0)); // another client's start
    REQUIRE_FALSE(history.Accept(QuestEventHistory::StageUpdate, 10));
    REQUIRE(history.Accept(QuestEventHistory::StageUpdate, 30));
    REQUIRE(history.Accept(QuestEventHistory::StageUpdate, 20)); // branches need not arrive numerically
    REQUIRE_FALSE(history.Accept(QuestEventHistory::StageUpdate, 30));
    REQUIRE(history.Accept(QuestEventHistory::Stopped, 30));
    REQUIRE_FALSE(history.Accept(QuestEventHistory::Stopped, 30));
    REQUIRE_FALSE(history.Accept(QuestEventHistory::StageUpdate, 10));
    REQUIRE(history.Accept(QuestEventHistory::Started, 0)); // explicit restart of a repeatable quest
    REQUIRE(history.Accept(QuestEventHistory::StageUpdate, 10));
    REQUIRE_FALSE(history.Accept(255, 10));
}

TEST_CASE("A delayed first start does not erase stages received from another member", "[quest.history]")
{
    QuestEventHistory history;
    REQUIRE(history.Accept(QuestEventHistory::StageUpdate, 10));
    REQUIRE(history.Accept(QuestEventHistory::Started, 0));
    REQUIRE_FALSE(history.Accept(QuestEventHistory::StageUpdate, 10));
}
