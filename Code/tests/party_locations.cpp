#include <EncodingPch.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <catch2/catch.hpp>
#include <Messages/ServerMessageFactory.h>
#include <limits>

TEST_CASE("Party map snapshots are bounded, epoch scoped and preserve locations", "[party.locations]")
{
    NotifyPartyLocations source;
    source.LeaderPlayerId = 10;
    source.Sequence = 900;
    source.Tick = 10000;
    for (uint32_t id = 0; id < NotifyPartyLocations::kMaxPlayers; ++id)
    {
        NotifyPartyLocations::Location location;
        location.PlayerId = id;
        location.ServerId = id + 20;
        location.OwnershipEpoch = id + 1;
        location.ReceivedTick = source.Tick - 500;
        location.Cell = GameId(0x1000, id + 1);
        location.WorldSpace = id % 2 ? GameId{} : GameId(0, 0x3C);
        location.Position = glm::vec3(-1048575.f, 100.f + id, 1048575.f);
        source.Players.push_back(location);
    }
    REQUIRE(source.IsValid());
    TiltedPhoques::Buffer buffer(2048);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    source.Serialize(writer, 17);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    const auto decoded = ServerMessageFactory{}.Extract(reader, 17);
    REQUIRE(decoded);
    const auto& result = *static_cast<const NotifyPartyLocations*>(decoded.get());
    REQUIRE(result.IsValid());
    REQUIRE(result.Sequence == 900);
    REQUIRE(result.Tick == source.Tick);
    REQUIRE(result.Players.size() == source.Players.size());
    for (size_t i = 0; i < source.Players.size(); ++i)
    {
        REQUIRE(result.Players[i].PlayerId == source.Players[i].PlayerId);
        REQUIRE(result.Players[i].ServerId == source.Players[i].ServerId);
        REQUIRE(result.Players[i].Cell == source.Players[i].Cell);
        REQUIRE(result.Players[i].WorldSpace == source.Players[i].WorldSpace);
        REQUIRE(result.Players[i].OwnershipEpoch == source.Players[i].OwnershipEpoch);
        REQUIRE(result.Players[i].Position == source.Players[i].Position);
    }
    TiltedPhoques::Buffer::Reader oldWorld(&buffer);
    REQUIRE_FALSE(ServerMessageFactory{}.Extract(oldWorld, 18));
    TiltedPhoques::Buffer body(2048);
    TiltedPhoques::Buffer::Writer bodyWriter(&body);
    source.SerializeRaw(bodyWriter);
    for (size_t length = 0; length < bodyWriter.Size(); ++length)
    {
        TiltedPhoques::ViewBuffer view(body.GetWriteData(), length);
        TiltedPhoques::Buffer::Reader partial(&view);
        NotifyPartyLocations invalid;
        invalid.DeserializeRaw(partial);
        REQUIRE_FALSE(invalid.IsValid());
    }
}

TEST_CASE("Party map snapshots reject duplicate identities, stale positions and unsafe vectors", "[party.locations]")
{
    NotifyPartyLocations snapshot;
    snapshot.Sequence = 1;
    snapshot.Tick = 10000;
    NotifyPartyLocations::Location player;
    player.PlayerId = 0;
    player.ServerId = 0;
    player.OwnershipEpoch = 1;
    player.ReceivedTick = snapshot.Tick;
    player.Cell = GameId(0, 123);
    REQUIRE(player.IsValid(snapshot.Tick));
    player.ReceivedTick = snapshot.Tick + 1;
    REQUIRE_FALSE(player.IsValid(snapshot.Tick));
    player.ReceivedTick = snapshot.Tick - NotifyPartyLocations::kMaxAgeMs - 1;
    REQUIRE_FALSE(player.IsValid(snapshot.Tick));
    player.ReceivedTick = snapshot.Tick;
    player.Position.x = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(player.IsValid(snapshot.Tick));
    player.Position.x = 1048576.f;
    REQUIRE_FALSE(player.IsValid(snapshot.Tick));
    player.Position.x = 0.f;
    snapshot.Players.push_back(player);
    snapshot.Players.push_back(player);
    REQUIRE_FALSE(snapshot.IsValid());
    snapshot.Players[1].PlayerId = 1;
    REQUIRE_FALSE(snapshot.IsValid());
    snapshot.Players[1].ServerId = 1;
    REQUIRE(snapshot.IsValid());
    snapshot.Players.resize(9);
    REQUIRE_FALSE(snapshot.IsValid());
    // A forged size is rejected before allocating or decoding any entry.
    TiltedPhoques::Buffer buffer(128);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(1, 1);
    writer.WriteBits(0, 32);
    writer.WriteBits(1, 64);
    writer.WriteBits(100, 64);
    writer.WriteBits(255, 8);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    NotifyPartyLocations decoded;
    decoded.DeserializeRaw(reader);
    REQUIRE_FALSE(decoded.IsValid());
    REQUIRE(decoded.Players.empty());
}
