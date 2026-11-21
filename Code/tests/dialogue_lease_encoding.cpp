#include <EncodingPch.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <catch2/catch.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include "../common/TimedReferenceSet.h"

TEST_CASE("Dialogue lease codecs preserve authority and reject every truncated body", "[dialogue.encoding]")
{
    RequestDialogueLease request;
    request.ServerId = 12;
    request.OwnershipEpoch = 99;
    request.Sequence = UINT64_MAX;
    request.Tick = 123456;
    request.Active = true;
    NotifyDialogueLease notify;
    notify.ServerId = request.ServerId;
    notify.OwnershipEpoch = request.OwnershipEpoch;
    notify.SpeakerPlayerId = 17;
    notify.Revision = 345;
    notify.Deadline = request.Tick + RequestDialogueLease::kDurationMs;
    notify.Active = true;
    TiltedPhoques::Buffer requestBuffer(128);
    TiltedPhoques::Buffer::Writer requestWriter(&requestBuffer);
    request.Serialize(requestWriter, 100);
    TiltedPhoques::Buffer::Reader requestReader(&requestBuffer);
    const auto decodedRequest = ClientMessageFactory{}.Extract(requestReader, 100);
    REQUIRE(decodedRequest);
    const auto& decoded = *static_cast<const RequestDialogueLease*>(decodedRequest.get());
    REQUIRE(decoded.IsValid());
    REQUIRE(decoded.OwnershipEpoch == 99);
    REQUIRE(decoded.Sequence == UINT64_MAX);
    REQUIRE(decoded.ServerId == 12);
    REQUIRE(decoded.Tick == request.Tick);
    REQUIRE(decoded.Active);
    TiltedPhoques::Buffer::Reader staleReader(&requestBuffer);
    REQUIRE_FALSE(ClientMessageFactory{}.Extract(staleReader, 101));

    TiltedPhoques::Buffer notifyBuffer(128);
    TiltedPhoques::Buffer::Writer notifyWriter(&notifyBuffer);
    notify.Serialize(notifyWriter, 100);
    TiltedPhoques::Buffer::Reader notifyReader(&notifyBuffer);
    const auto decodedNotify = ServerMessageFactory{}.Extract(notifyReader, 100);
    REQUIRE(decodedNotify);
    const auto& result = *static_cast<const NotifyDialogueLease*>(decodedNotify.get());
    REQUIRE(result.IsValid());
    REQUIRE(result.SpeakerPlayerId == 17);
    REQUIRE(result.Deadline == notify.Deadline);
    REQUIRE(result.Revision == 345);
    REQUIRE(result.OwnershipEpoch == 99);
    REQUIRE(result.ServerId == 12);
    REQUIRE(result.Active);
    TiltedPhoques::Buffer::Reader staleNotifyReader(&notifyBuffer);
    REQUIRE_FALSE(ServerMessageFactory{}.Extract(staleNotifyReader, 101));

    auto checkTruncation = [](const auto& source)
    {
        TiltedPhoques::Buffer body(128);
        TiltedPhoques::Buffer::Writer writer(&body);
        source.SerializeRaw(writer);
        for (size_t size = 0; size < writer.Size(); ++size)
        {
            TiltedPhoques::ViewBuffer view(body.GetWriteData(), size);
            TiltedPhoques::Buffer::Reader reader(&view);
            std::decay_t<decltype(source)> invalid;
            invalid.DeserializeRaw(reader);
            REQUIRE_FALSE(invalid.IsValid());
        }
    };
    checkTruncation(request);
    checkTruncation(notify);
    request.OwnershipEpoch = 0;
    REQUIRE_FALSE(request.IsValid());
    notify.Deadline = 0;
    REQUIRE_FALSE(notify.IsValid());
    notify.Active = false;
    REQUIRE(notify.IsValid());
    notify.Revision = 0;
    REQUIRE_FALSE(notify.IsValid());
}

TEST_CASE("Bounded reference deadlines expire and survive a 32-bit clock wrap", "[dialogue.deadline]")
{
    TimedReferenceSet refs;
    REQUIRE_FALSE(refs.Set(0, 2000, 1000));
    REQUIRE_FALSE(refs.Set(1, 1000, 1000));
    REQUIRE_FALSE(refs.Set(1, 999, 1000));
    for (uint32_t id = 1; id <= TimedReferenceSet::kCapacity; ++id)
        REQUIRE(refs.Set(id, 2000, 1000));
    REQUIRE_FALSE(refs.Set(99, 2000, 1000));
    REQUIRE(refs.Contains(1, 1999));
    REQUIRE_FALSE(refs.Contains(1, 2000));
    REQUIRE_FALSE(refs.Contains(1, 2001));
    REQUIRE(refs.Set(99, 2100, 2000));
    REQUIRE_FALSE(refs.Contains(1, 2000));
    REQUIRE(refs.Contains(99, 2000));
    refs.Remove(99);
    REQUIRE_FALSE(refs.Contains(99, 2000));
    refs.Clear();
    const uint32_t now = UINT32_MAX - 500;
    REQUIRE(refs.Set(42, now + 3000, now));
    REQUIRE(refs.Contains(42, now));
    REQUIRE(refs.Contains(42, 0));
    REQUIRE(refs.Contains(42, now + 2999));
    REQUIRE_FALSE(refs.Contains(42, now + 3000));
    refs.Clear();
    REQUIRE_FALSE(refs.Contains(42, now));
}
