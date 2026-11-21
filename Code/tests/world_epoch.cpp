#include <EncodingPch.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <catch2/catch.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>

using namespace TiltedPhoques;

TEST_CASE("Gameplay envelopes carry and reject another world timeline before payload decoding", "[world.epoch]")
{
    RequestCampaignReady request;
    request.Nonce = 11;
    request.Revision = 30;
    Buffer buffer(128);
    Buffer::Writer writer(&buffer);
    request.Serialize(writer, 123456789);
    Buffer::Reader reader(&buffer);
    const auto decoded = ClientMessageFactory{}.Extract(reader, 123456789);
    REQUIRE(decoded);
    REQUIRE(decoded->WorldEpoch == 123456789);
    REQUIRE(static_cast<const RequestCampaignReady*>(decoded.get())->Nonce == 11);
    Buffer::Reader old(&buffer);
    REQUIRE_FALSE(ClientMessageFactory{}.Extract(old, 123456790));

    // No payload at all: an old packet must be rejected without calling its decoder.
    Buffer header(32);
    Buffer::Writer headerWriter(&header);
    headerWriter.WriteBits(kNotifyInventoryChanges, 8);
    headerWriter.WriteBits(12, 64);
    Buffer::Reader oldHeader(&header);
    REQUIRE_FALSE(ServerMessageFactory{}.Extract(oldHeader, 13));

    ServerTimeSettings time;
    Buffer response(128);
    Buffer::Writer responseWriter(&response);
    time.Serialize(responseWriter, 900);
    Buffer::Reader responseReader(&response);
    const auto serverMessage = ServerMessageFactory{}.Extract(responseReader, 900);
    REQUIRE(serverMessage);
    REQUIRE(serverMessage->WorldEpoch == 900);
    Buffer::Reader oldResponse(&response);
    REQUIRE_FALSE(ServerMessageFactory{}.Extract(oldResponse, 901));
}

TEST_CASE("Factories reject every truncated timeline header and invalid opcodes", "[world.epoch]")
{
    const std::array<uint8_t, 9> header{static_cast<uint8_t>(kRequestCampaignReady), 1, 0, 0, 0, 0, 0, 0, 0};
    for (size_t size = 0; size < header.size(); ++size)
    {
        ViewBuffer view(const_cast<uint8_t*>(header.data()), size);
        Buffer::Reader reader(&view);
        REQUIRE_FALSE(ClientMessageFactory{}.Extract(reader, 1));
        Buffer::Reader serverReader(&view);
        REQUIRE_FALSE(ServerMessageFactory{}.Extract(serverReader, 1));
    }
    Buffer bad(16);
    Buffer::Writer writer(&bad);
    writer.WriteBits(255, 8);
    writer.WriteBits(1, 64);
    Buffer::Reader clientReader(&bad);
    REQUIRE_FALSE(ClientMessageFactory{}.Extract(clientReader, 1));
    Buffer::Reader serverReader(&bad);
    REQUIRE_FALSE(ServerMessageFactory{}.Extract(serverReader, 1));
}
