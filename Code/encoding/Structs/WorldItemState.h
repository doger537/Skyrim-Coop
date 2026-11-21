#pragma once

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>
#include <glm/vec3.hpp>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>
#include <array>

// Stable within a campaign WorldId. Epoch identifies the current timeline, not
// the saved item: a rollback retains item IDs but invalidates old motion packets.
struct WorldItemMotion
{
    enum class Motion : uint8_t { Dynamic, Held, Sleeping, Kinematic };

    WorldItemMotion() noexcept { Position = glm::vec3(0.f); }

    uint64_t Id{};
    uint64_t WorldEpoch{};
    uint64_t HostEpoch{};
    uint64_t Revision{};
    uint64_t Sequence{};
    uint64_t Tick{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    // Unit quaternion in W, X, Y, Z order; world space, no Euler-axis truncation.
    std::array<float, 4> Rotation{1.f, 0.f, 0.f, 0.f};
    // Skyrim world units per second and radians per second, respectively.
    std::array<float, 3> LinearVelocity{};
    std::array<float, 3> AngularVelocity{};
    Motion Mode{Motion::Dynamic};
    bool Decoded{true};

    bool IsValid() const noexcept;
    bool operator==(const WorldItemMotion&) const noexcept = default;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};

// Creation/lifecycle metadata is reliable and infrequent. Motion packets use
// only the base POD fields, with no inventory vector allocation or copying.
struct WorldItemState : WorldItemMotion
{
    static constexpr size_t kMaxEffects = 64;
    Inventory::Entry Item{};
    bool Deleted{};

    bool IsValid() const noexcept;
    bool operator==(const WorldItemState&) const noexcept = default;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
