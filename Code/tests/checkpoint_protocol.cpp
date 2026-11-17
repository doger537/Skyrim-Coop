#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>

namespace
{
WorldCheckpoint Manifest()
{
    WorldCheckpoint manifest;
    manifest.Id = 5; manifest.SavedEpoch = 3;
    for (size_t i = 0; i < 2; ++i)
    {
        auto& player = manifest.Players[i];
        player.Character.Parts[0] = static_cast<uint16_t>(i + 1);
        player.Save.Bytes = 100; player.Save.Digest.fill(static_cast<uint8_t>(i + 1));
    }
    return manifest;
}
}

TEST_CASE("Paired checkpoint names round trip full-width identities without accepting arbitrary paths", "[checkpoint.names]")
{
    const String world = "0123456789abcdef0123456789abcdef";
    CharacterIdentity character{{0, 1, 0xFFFF, 3, 4, 5, 6, 7}};
    const auto name = CheckpointNames::Make(std::string_view(world.data(), world.size()), UINT64_MAX, character);
    REQUIRE(name.size() == 91);
    String decodedWorld;
    uint64_t id{};
    CharacterIdentity decodedCharacter;
    REQUIRE(CheckpointNames::Parse(std::string_view(name.data(), name.size()), decodedWorld, id, decodedCharacter));
    REQUIRE(decodedWorld == world);
    REQUIRE(id == UINT64_MAX);
    REQUIRE(decodedCharacter == character);
    const auto withExtension = name + ".ess";
    REQUIRE(CheckpointNames::Parse(std::string_view(withExtension.data(), withExtension.size()), decodedWorld, id, decodedCharacter));
    for (size_t i = 0; i < name.size(); ++i)
    {
        auto malformed = name;
        malformed[i] = '/';
        REQUIRE_FALSE(CheckpointNames::Parse(std::string_view(malformed.data(), malformed.size()), decodedWorld, id, decodedCharacter));
    }
    REQUIRE(CheckpointNames::Make("../other", 1, character).empty());
    REQUIRE(CheckpointNames::Make(std::string_view(world.data(), world.size()), 0, character).empty());
    REQUIRE(CheckpointNames::Make(std::string_view(world.data(), world.size()), 1, {}).empty());
}

TEST_CASE("Paired checkpoint control is bounded and rejects every truncated restore manifest", "[checkpoint.protocol]")
{
    NotifyWorldCheckpoint source;
    source.State = NotifyWorldCheckpoint::Phase::PrepareLoad;
    source.Id = 5; source.Nonce = 8; source.PreviousEpoch = 3;
    source.WorldId = "0123456789abcdef0123456789abcdef";
    source.Manifest = Manifest();
    for (size_t i = 0; i < 2; ++i)
        source.Members[i] = source.Manifest.Players[i].Character;
    REQUIRE(source.IsValid());
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    source.Serialize(writer, 3);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    auto message = ServerMessageFactory{}.Extract(reader, 3);
    REQUIRE(message);
    const auto& decoded = *static_cast<const NotifyWorldCheckpoint*>(message.get());
    REQUIRE(decoded.IsValid());
    REQUIRE(decoded.Manifest.Players == source.Manifest.Players);
    REQUIRE(decoded.WorldId == source.WorldId);
    for (size_t length = 0; length < writer.Size(); ++length)
    {
        TiltedPhoques::ViewBuffer partial(buffer.GetWriteData(), length);
        TiltedPhoques::Buffer::Reader input(&partial);
        auto truncated = ServerMessageFactory{}.Extract(input, 3);
        if (truncated)
            REQUIRE_FALSE(static_cast<const NotifyWorldCheckpoint*>(truncated.get())->IsValid());
    }
    source.Manifest.Players[1].Character = source.Manifest.Players[0].Character;
    REQUIRE_FALSE(source.IsValid());
    source.Manifest = Manifest();
    source.Manifest.Players[0].Save.Bytes = CheckpointFile::kMaxBytes + 1;
    REQUIRE_FALSE(source.IsValid());
}

TEST_CASE("A saved-file contribution cannot be empty, oversized or truncated", "[checkpoint.protocol]")
{
    RequestWorldCheckpoint request;
    request.Action = RequestWorldCheckpoint::Kind::Saved;
    request.Id = 5; request.Nonce = 8;
    request.Contribution = Manifest().Players[0];
    REQUIRE(request.IsValid());
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    request.Serialize(writer, 3);
    for (size_t length = 0; length < writer.Size(); ++length)
    {
        TiltedPhoques::ViewBuffer partial(buffer.GetWriteData(), length);
        TiltedPhoques::Buffer::Reader input(&partial);
        auto message = ClientMessageFactory{}.Extract(input, 3);
        if (message)
            REQUIRE_FALSE(static_cast<const RequestWorldCheckpoint*>(message.get())->IsValid());
    }
    request.Contribution.Save.Digest.fill(0);
    REQUIRE_FALSE(request.IsValid());
    request.Contribution = Manifest().Players[0];
    request.Contribution.CoSave.Bytes = 1;
    REQUIRE_FALSE(request.IsValid());
}

TEST_CASE("Only a valid directed load advances the checkpoint message epoch", "[checkpoint.protocol]")
{
    NotifyWorldCheckpoint source;
    source.State = NotifyWorldCheckpoint::Phase::LoadNow;
    source.Id = 5; source.Nonce = 8; source.PreviousEpoch = 3;
    source.WorldId = "0123456789abcdef0123456789abcdef";
    source.Manifest = Manifest();
    for (size_t i = 0; i < 2; ++i)
        source.Members[i] = source.Manifest.Players[i].Character;
    const auto extract = [&](uint64_t epoch, uint64_t expected)
    {
        TiltedPhoques::Buffer buffer(4096);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        source.Serialize(writer, epoch);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        return ServerMessageFactory{}.Extract(reader, expected);
    };
    REQUIRE(source.IsValid());
    REQUIRE(extract(10, 3));
    REQUIRE_FALSE(extract(2, 3));
    REQUIRE_FALSE(extract(10, 4));
    source.State = NotifyWorldCheckpoint::Phase::PrepareLoad;
    REQUIRE_FALSE(extract(10, 3));
    source.State = NotifyWorldCheckpoint::Phase::Resume;
    REQUIRE_FALSE(extract(10, 3));
    source.State = NotifyWorldCheckpoint::Phase::LoadNow;
    source.Manifest.Players[0].Save.Digest.fill(0);
    REQUIRE_FALSE(extract(10, 3));
}
