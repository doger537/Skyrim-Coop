#pragma once
#include <TiltedCore/Buffer.hpp>
#include <array>

struct RigidBodyPose
{
    std::array<float, 3> Position{};
    std::array<float, 4> Rotation{1.f, 0.f, 0.f, 0.f}; // WXYZ
    std::array<float, 3> LinearVelocity{};
    std::array<float, 3> AngularVelocity{};
    bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
struct RigidBodyRig
{
    static constexpr size_t kMaxBodies = 32;
    struct Body { uint64_t Key{}; RigidBodyPose Pose; };
    uint8_t Count{};
    std::array<Body, kMaxBodies> Bodies{}; // sorted unique stable node-path keys
    bool IsValid() const noexcept;
    bool SameGraph(const RigidBodyRig& acOther) const noexcept;
    static bool Blend(const RigidBodyRig& acFirst, const RigidBodyRig& acSecond, float aDelta, RigidBodyRig& aOutput) noexcept;
    static bool Predict(const RigidBodyRig& acSource, float aSeconds, RigidBodyRig& aOutput) noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
