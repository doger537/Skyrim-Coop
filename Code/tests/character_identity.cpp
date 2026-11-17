#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <Structs/CharacterIdentity.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/ClientMessageFactory.h>
#include <CampaignState.h>
#include <limits>

TEST_CASE("Saved character words round trip through exact floats without accepting damaged values", "[character.identity]")
{
    CharacterIdentity identity;
    REQUIRE_FALSE(identity.IsValid());
    std::array<float, 8> values{0.f, 1.f, 255.f, 256.f, 32767.f, 32768.f, 65534.f, 65535.f};
    REQUIRE(CharacterIdentity::FromGlobals(values, identity));
    REQUIRE(identity.IsValid());
    for (size_t i = 0; i < values.size(); ++i)
        REQUIRE(float(identity.Parts[i]) == values[i]);
    const auto original = identity;
    for (float bad : {-1.f, 0.5f, 65536.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        for (size_t i = 0; i < values.size(); ++i)
        {
            auto damaged = values;
            damaged[i] = bad;
            REQUIRE_FALSE(CharacterIdentity::FromGlobals(damaged, identity));
            REQUIRE(identity == original);
        }
}

TEST_CASE("Authentication character IDs reject zero and every truncated fixed identity prefix", "[character.protocol]")
{
    AuthenticationRequest source;
    source.CharacterId.Parts = {1, 0, 65535, 4, 5, 6, 7, 8};
    source.Username = "Same name is not identity";
    TiltedPhoques::Buffer buffer(2048);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    source.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    auto decoded = ClientMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    const auto& request = *static_cast<const AuthenticationRequest*>(decoded.get());
    REQUIRE(request.CharacterIdentityDecoded);
    REQUIRE(request == source);
    // Opcode + world epoch + protocol precede the fixed 128-bit identity.
    constexpr size_t prefix = 1 + 8 + 4;
    for (size_t length = 0; length < prefix + 16; ++length)
    {
        TiltedPhoques::ViewBuffer partial(buffer.GetWriteData(), length);
        TiltedPhoques::Buffer::Reader partialReader(&partial);
        auto message = ClientMessageFactory{}.Extract(partialReader);
        if (message)
            REQUIRE_FALSE(static_cast<const AuthenticationRequest*>(message.get())->CharacterIdentityDecoded);
    }
    CharacterIdentity zero;
    TiltedPhoques::Buffer zeroBuffer(16);
    TiltedPhoques::Buffer::Writer zeroWriter(&zeroBuffer);
    zero.Serialize(zeroWriter);
    TiltedPhoques::Buffer::Reader zeroReader(&zeroBuffer);
    REQUIRE_FALSE(zero.Deserialize(zeroReader));
}

TEST_CASE("Character enrollment survives restart and reconnect without consuming new world revisions", "[character.persistence]")
{
    const auto path = std::filesystem::current_path() / "character-enrollment-test.sqlite3";
    REQUIRE_FALSE(std::filesystem::exists(path));
    CharacterIdentity first{{1, 2, 3, 4, 5, 6, 7, 8}};
    CharacterIdentity second{{1, 2, 3, 4, 5, 6, 7, 9}};
    uint64_t revision{};
    Mods content;
    content.ModList = {{"Skyrim.esm", 0, false}};
    content.ModList[0].Digest.fill(42);
    content.Resources = {{"runtime/address-library"}, {"data/loose-files"}};
    for (auto& resource : content.Resources)
        resource.Digest.fill(43);
    {
        Campaign::State state;
        REQUIRE(state.Open(path));
        REQUIRE_FALSE(state.RegisterParticipant(first));
        REQUIRE(state.Bind("1.7.104.0", content));
        REQUIRE_FALSE(state.RegisterParticipant({}));
        REQUIRE(state.RegisterParticipant(first));
        revision = state.Revision();
        REQUIRE(state.RegisterParticipant(first));
        REQUIRE(state.Revision() == revision);
        REQUIRE(state.RegisterParticipant(second));
        REQUIRE(state.Revision() == revision + 1);
        revision = state.Revision();
    }
    {
        Campaign::State reopened;
        REQUIRE(reopened.Open(path));
        REQUIRE(reopened.RegisterParticipant(first));
        REQUIRE(reopened.RegisterParticipant(second));
        REQUIRE(reopened.Revision() == revision);
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    REQUIRE_FALSE(error);
}
