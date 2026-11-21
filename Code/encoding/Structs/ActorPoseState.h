#pragma once
#include <Structs/GameId.h>

// Discrete engine mode on the owner's movement timeline. Never interpolate
// sit/ragdoll transitions or write the complete native actor flags from this.
struct ActorPoseState
{
    bool Known{};
    bool Ragdoll{};
    uint8_t SitSleep{};
    uint8_t Knock{};
    uint8_t Life{};
    GameId Furniture{};
    uint32_t FurnitureMarker{UINT32_MAX};
    bool IsValid() const noexcept;
    bool IsDriven() const noexcept;
    bool operator==(const ActorPoseState&) const noexcept = default;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
