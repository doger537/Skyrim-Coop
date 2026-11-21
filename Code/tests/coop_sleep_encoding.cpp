#include <EncodingPch.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <catch2/catch.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>

TEST_CASE("Rest protocol preserves the voting transaction and committed calendar", "[coop.sleep.encoding]")
{
    RequestCoopSleep request;
    request.Action = CoopSleepVote::Action::Vote;
    request.Sequence = 42;
    request.Round = 900;
    request.Tick = 1234567;
    request.Hours = 8;
    TiltedPhoques::Buffer requestBuffer(128);
    TiltedPhoques::Buffer::Writer requestWriter(&requestBuffer);
    request.Serialize(requestWriter, 99);
    TiltedPhoques::Buffer::Reader requestReader(&requestBuffer);
    const auto decodedRequest = ClientMessageFactory{}.Extract(requestReader, 99);
    REQUIRE(decodedRequest);
    const auto& received = *static_cast<const RequestCoopSleep*>(decodedRequest.get());
    REQUIRE(received.IsValid());
    REQUIRE(received.Sequence == request.Sequence);
    REQUIRE(received.Round == request.Round);
    REQUIRE(received.Tick == request.Tick);
    REQUIRE(received.Hours == request.Hours);

    NotifyCoopSleep commit;
    commit.Phase = CoopSleepVote::Phase::Committed;
    commit.Members = {10, 20};
    commit.Round = 900;
    commit.Hours = 4;
    commit.Votes = 3;
    commit.Deadline = 1234599;
    commit.Tick = 1234567;
    commit.Time.DaysPassed = 5000.75;
    TiltedPhoques::Buffer buffer(256);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    commit.Serialize(writer, 99);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    const auto decoded = ServerMessageFactory{}.Extract(reader, 99);
    REQUIRE(decoded);
    const auto& result = *static_cast<const NotifyCoopSleep*>(decoded.get());
    REQUIRE(result.IsValid());
    REQUIRE(result.Time == commit.Time);
    REQUIRE(result.Members == commit.Members);
    REQUIRE(result.Tick == commit.Tick);
    REQUIRE(result.Round == commit.Round);
    REQUIRE(result.Hours == 4);

    // Check the body independently: an incomplete commit cannot become a time skip.
    TiltedPhoques::Buffer body(256);
    TiltedPhoques::Buffer::Writer bodyWriter(&body);
    commit.SerializeRaw(bodyWriter);
    for (size_t size = 0; size < bodyWriter.Size(); ++size)
    {
        TiltedPhoques::ViewBuffer view(body.GetWriteData(), size);
        TiltedPhoques::Buffer::Reader truncated(&view);
        NotifyCoopSleep invalid;
        invalid.DeserializeRaw(truncated);
        REQUIRE_FALSE(invalid.IsValid());
    }
}
