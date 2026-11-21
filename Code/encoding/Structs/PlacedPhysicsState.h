#pragma once
#include <Structs/GameId.h>
#include <Structs/RigidBodyPose.h>

struct PlacedPhysicsState
{
    static constexpr size_t kMaxRecords = 8192;
    GameId Reference, Cell, WorldSpace;
    uint32_t Owner{};
    uint64_t Epoch{}, Sequence{}, Tick{};
    RigidBodyRig Rig;
    bool Retired{}, Decoded{true};
    bool IsValid() const noexcept { return Decoded && Reference && Cell && Owner && Epoch && Sequence && Tick && Rig.IsValid(); }
    void Serialize(TiltedPhoques::Buffer::Writer& w) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& r) noexcept;
};
