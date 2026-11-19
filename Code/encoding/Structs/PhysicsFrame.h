#pragma once
#include <Structs/GameId.h>
#include <Structs/RigidBodyPose.h>
#include <TiltedCore/Stl.hpp>
#include <array>

struct PhysicsFrameEntry
{
    enum Kind : uint8_t
    {
        Placed,
        Drop,
        Actor
    };
    uint8_t Type{}, SourceSlot{}, PresentMask{};
    GameId Reference, Cell, WorldSpace;
    // Actor IDs identify a record INSIDE ONE checkpoint, not a live actor after
    // reload. Each client's private, hashed .tcphys maps that key to its own ESS.
    uint64_t Id{}, Epoch{};
    RigidBodyRig Rig; // Native Havok floats, never quantized/scaled.
    bool IsValid() const noexcept;
    bool SameKey(const PhysicsFrameEntry&) const noexcept;
    static bool Less(const PhysicsFrameEntry&, const PhysicsFrameEntry&) noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer&) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader&) noexcept;
};
struct PhysicsFrame
{
    static constexpr size_t kMaxEntries = 8192, kMaxBytes = 20 * 1024 * 1024;
    using Digest = std::array<uint8_t, 32>;
    uint64_t Checkpoint{};
    float NativeToWorld{};
    TiltedPhoques::Vector<PhysicsFrameEntry> Entries;
    bool IsValid() const noexcept;
    bool Encode(TiltedPhoques::Vector<uint8_t>&) const noexcept;
    static bool Decode(const TiltedPhoques::Vector<uint8_t>&, PhysicsFrame&) noexcept;
};
