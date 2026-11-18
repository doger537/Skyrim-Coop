#pragma once
#include <Structs/RigidBodyPose.h>
#include <Structs/GameId.h>
struct RagdollSnapshot
{
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    uint64_t Sequence{};
    uint64_t Tick{};
    bool Interaction{}; // guest input to the owner; never a canonical broadcast
    GameId Cell{}, WorldSpace{};
    RigidBodyRig Rig;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && OwnershipEpoch && Sequence && Tick && Cell && Rig.IsValid(); }
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
