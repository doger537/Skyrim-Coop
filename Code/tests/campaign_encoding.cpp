#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>

using namespace TiltedPhoques;

template <class T, class Factory> T RoundTrip(const T& message)
{
    Buffer buffer(65536);
    Buffer::Writer writer(&buffer);
    message.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = Factory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == message.GetOpcode());
    return *static_cast<T*>(decoded.get());
}

TEST_CASE("Campaign barrier and baseline messages survive factory encoding", "[campaign.encoding]")
{
    RequestCampaignReady ready;
    ready.Nonce = 124;
    ready.Revision = 4000;
    ready.SeedCount = 4095;
    auto decoded = RoundTrip<RequestCampaignReady, ClientMessageFactory>(ready);
    REQUIRE(decoded.Nonce == ready.Nonce);
    REQUIRE(decoded.Revision == ready.Revision);
    REQUIRE(decoded.SeedCount == ready.SeedCount);
    ready.SeedCount = UINT32_MAX;
    REQUIRE(RoundTrip<RequestCampaignReady, ClientMessageFactory>(ready).SeedCount == UINT32_MAX);
    RequestCampaignSeed seed;
    seed.Nonce = 124;
    seed.Data.Id = GameId(78, 0x800);
    seed.Data.Type = 8;
    seed.Data.Started = true;
    seed.Data.Stopped = true;
    seed.Data.Events = {{1, 0, true}, {0, 30, true}, {0, 20, true}, {2, 20, true}};
    const auto imported = RoundTrip<RequestCampaignSeed, ClientMessageFactory>(seed);
    REQUIRE(imported.Nonce == seed.Nonce);
    REQUIRE(imported.Data == seed.Data);
    NotifyQuestJournal journal;
    journal.Nonce = seed.Nonce;
    journal.Revision = 1234;
    journal.Data = seed.Data;
    journal.Data.Decisions = {{"side", 20}};
    journal.ReplayStages = {10, 20};
    journal.ReplayLifecycle = true;
    const auto shared = RoundTrip<NotifyQuestJournal, ServerMessageFactory>(journal);
    REQUIRE(shared.Data == journal.Data);
    REQUIRE(shared.Revision == journal.Revision);
    REQUIRE(shared.Nonce == journal.Nonce);
    REQUIRE(shared.ReplayStages == journal.ReplayStages);
    REQUIRE(shared.ReplayLifecycle);
    NotifyCampaignCheckpoint checkpoint;
    checkpoint.State = NotifyCampaignCheckpoint::End;
    checkpoint.WorldId = "0123456789abcdef0123456789abcdef";
    checkpoint.Nonce = 44;
    checkpoint.Revision = 99;
    checkpoint.Count = 4095;
    checkpoint.SeedOwner = true;
    checkpoint.BaselinePending = true;
    checkpoint.AllowMisc = true;
    checkpoint.StartupDiagnosticOnly = true;
    checkpoint.MiscQuests = {GameId(78, 0x800)};
    const auto boundary = RoundTrip<NotifyCampaignCheckpoint, ServerMessageFactory>(checkpoint);
    REQUIRE(boundary.Decoded);
    REQUIRE(boundary.State == checkpoint.State);
    REQUIRE(boundary.WorldId == checkpoint.WorldId);
    REQUIRE(boundary.Count == checkpoint.Count);
    REQUIRE(boundary.Nonce == checkpoint.Nonce);
    REQUIRE(boundary.Revision == checkpoint.Revision);
    REQUIRE(boundary.SeedOwner);
    REQUIRE(boundary.BaselinePending);
    REQUIRE(boundary.AllowMisc);
    REQUIRE(boundary.MiscQuests == checkpoint.MiscQuests);
}

TEST_CASE("Quest admission diagnostics carry bounded quest observations", "[campaign.encoding][quest.admission]")
{
    RequestCampaignReady message;
    message.State = RequestCampaignReady::Detail;
    message.Nonce = 88;
    message.Revision = 5;
    message.Diff.Id = GameId(0, 0x2BF9C);
    message.Diff.LocalForm = 0x2BF9C;
    message.Diff.Type = 1;
    message.Diff.EditorId = "MQ102A";
    message.Diff.Local = {true, false, {3, 5, 10}};
    message.Diff.Problem = QuestAdmissionProblem::LocalProgress;
    const auto decoded = RoundTrip<RequestCampaignReady, ClientMessageFactory>(message);
    REQUIRE(decoded.State == RequestCampaignReady::Detail);
    REQUIRE(decoded.Diff.IsValid());
    REQUIRE(decoded.Diff == message.Diff);
    const QuestJournal journal{message.Diff.Id, 1, 1, true, false, {{0, 5, true}, {0, 10, true}, {0, 20, false}}};
    const auto formatted = DescribeQuestAdmission(decoded.Diff, &journal);
    REQUIRE(formatted.find("Bootstrap=[5,10]") != String::npos);
    REQUIRE(formatted.find("Both=[5,10] LocalOnly=[3] ServerOnly=[20]") != String::npos);
    REQUIRE(formatted.find("Epoch=unknown") != String::npos);
    message.Diff.EditorId.assign(QuestAdmissionDiff::kMaxEditorId + 1, 'x');
    REQUIRE_FALSE(RoundTrip<RequestCampaignReady, ClientMessageFactory>(message).Diff.IsValid());
}

TEST_CASE("Quest journal encoding rejects oversized or invalid state", "[campaign.encoding]")
{
    RequestCampaignSeed seed;
    seed.Data.Id = GameId(78, 0x800);
    seed.Data.Type = 8;
    seed.Data.Events.resize(QuestJournal::kMaxEvents);
    for (size_t i = 0; i < seed.Data.Events.size(); ++i)
        seed.Data.Events[i].Stage = static_cast<uint16_t>(i);
    REQUIRE(RoundTrip<RequestCampaignSeed, ClientMessageFactory>(seed).Data == seed.Data);
    seed.Data.Events.push_back({});
    REQUIRE_FALSE(RoundTrip<RequestCampaignSeed, ClientMessageFactory>(seed).Data.IsValid());
    seed.Data.Events.clear();
    seed.Data.Epoch = 0;
    REQUIRE_FALSE(RoundTrip<RequestCampaignSeed, ClientMessageFactory>(seed).Data.IsValid());
    NotifyCampaignCheckpoint checkpoint;
    checkpoint.WorldId = "0123456789abcdef0123456789abcdef";
    checkpoint.Count = 4096;
    REQUIRE_FALSE(RoundTrip<NotifyCampaignCheckpoint, ServerMessageFactory>(checkpoint).Decoded);
}
