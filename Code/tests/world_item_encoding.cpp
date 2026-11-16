#include <EncodingPch.h>
#include <catch2/catch.hpp>
#include <Structs/WorldItemState.h>
#include <limits>

namespace
{
WorldItemState ItemState()
{
    WorldItemState state;
    state.Id = 100;
    state.WorldEpoch = 2;
    state.HostEpoch = 3;
    state.Revision = 4;
    state.Sequence = 5;
    state.Tick = 6000;
    state.Item.BaseId = GameId(0, 0x12eb7);
    state.Item.Count = 12;
    state.Item.ExtraHealth = 1.25f;
    state.Item.ExtraCharge = 150.f;
    state.Item.ExtraEnchantId = GameId(1, 0x800);
    state.Item.ExtraEnchantCharge = 500;
    state.Item.EnchantData.IsWeapon = true;
    state.Item.EnchantData.Effects.push_back({12.f, 2, 10, 3.5f, GameId(0, 0x4605a)});
    state.CellId = GameId(0, 0x978);
    state.WorldSpaceId = GameId(0, 0x3c);
    state.Position = glm::vec3(123.f, -456.f, 789.f);
    state.Rotation = {0.70710678f, 0.f, 0.70710678f, 0.f};
    state.LinearVelocity = {123.25f, -12.125f, 0.f};
    state.AngularVelocity = {0.125f, -1.25f, 0.5f};
    return state;
}
}

TEST_CASE("World item state preserves instance identity, full rotation and item properties", "[world-item.encoding]")
{
    WorldItemState state = ItemState();
    REQUIRE(state.IsValid());
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    state.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    WorldItemState decoded;
    decoded.Deserialize(reader);
    REQUIRE(decoded.IsValid());
    REQUIRE(decoded == state);
    state.Deleted = true;
    state.Revision++;
    state.Mode = WorldItemState::Motion::Sleeping;
    writer.Reset();
    state.Serialize(writer);
    reader.Reset();
    decoded.Deserialize(reader);
    REQUIRE(decoded == state);
}

TEST_CASE("A truncated world item cannot be applied", "[world-item.encoding]")
{
    WorldItemState state = ItemState();
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    state.Serialize(writer);
    const size_t bytes = (writer.GetBitPosition() + 7) / 8;
    for (size_t count = 0; count < bytes; ++count)
    {
        TiltedPhoques::Buffer truncated(buffer.GetData(), count);
        TiltedPhoques::Buffer::Reader reader(&truncated);
        WorldItemState decoded;
        decoded.Deserialize(reader);
        REQUIRE_FALSE(decoded.IsValid());
    }
}

TEST_CASE("High-rate item motion excludes creation metadata", "[world-item.encoding]")
{
    const WorldItemMotion motion = ItemState();
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    motion.Serialize(writer);
    REQUIRE((writer.GetBitPosition() + 7) / 8 <= 128);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    WorldItemMotion decoded;
    decoded.Deserialize(reader);
    REQUIRE(decoded.IsValid());
    REQUIRE(decoded == motion);
}

TEST_CASE("World item effect counts are checked before allocation", "[world-item.encoding]")
{
    WorldItemState state = ItemState();
    const auto effect = state.Item.EnchantData.Effects.front();
    state.Item.EnchantData.Effects.assign(WorldItemState::kMaxEffects, effect);
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    state.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    WorldItemState decoded;
    decoded.Deserialize(reader);
    REQUIRE(decoded == state);

    writer.Reset();
    static_cast<const WorldItemMotion&>(state).Serialize(writer);
    // Fixed-width item prefix, then an adversarial count without any effects.
    writer.WriteBits(0, 64);
    writer.WriteBits(0, 64);
    writer.WriteBits(0, 64);
    writer.WriteBits(0, 16);
    writer.WriteBits(255, 8);
    reader.Reset();
    decoded.Deserialize(reader);
    REQUIRE_FALSE(decoded.IsValid());
    REQUIRE(decoded.Item.EnchantData.Effects.empty());
}

TEST_CASE("World item validation rejects unbounded and non-finite physics data", "[world-item.encoding]")
{
    WorldItemState state = ItemState();
    SECTION("timeline zero") { state.WorldEpoch = 0; }
    SECTION("instance zero") { state.Id = 0; }
    SECTION("invalid stack") { state.Item.Count = -1; }
    SECTION("position wrap") { state.Position.x = 1048576.f; }
    SECTION("position NaN") { state.Position.z = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("invalid quaternion") { state.Rotation = {}; }
    SECTION("velocity infinity") { state.LinearVelocity[1] = std::numeric_limits<float>::infinity(); }
    SECTION("angular velocity out of range") { state.AngularVelocity[2] = 65537.f; }
    SECTION("unknown motion") { state.Mode = static_cast<WorldItemState::Motion>(255); }
    SECTION("effect count") { state.Item.EnchantData.Effects.resize(WorldItemState::kMaxEffects + 1); }
    REQUIRE_FALSE(state.IsValid());
    TiltedPhoques::Buffer buffer(4096);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    state.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    WorldItemState decoded;
    decoded.Deserialize(reader);
    REQUIRE_FALSE(decoded.IsValid());
}
