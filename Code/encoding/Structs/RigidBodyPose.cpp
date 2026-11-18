#include "RigidBodyPose.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace
{
void Write(TiltedPhoques::Buffer::Writer& aWriter, float aValue) noexcept { aWriter.WriteBits(std::bit_cast<uint32_t>(aValue), 32); }
bool Read(TiltedPhoques::Buffer::Reader& aReader, float& aValue) noexcept
{
    uint64_t value{};
    if (!aReader.ReadBits(value, 32)) return false;
    aValue = std::bit_cast<float>(static_cast<uint32_t>(value));
    return std::isfinite(aValue);
}
}
bool RigidBodyPose::IsValid() const noexcept
{
    double norm{};
    for (float value : Rotation) { if (!std::isfinite(value)) return false; norm += double(value) * value; }
    if (std::abs(norm - 1.) > .01) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!std::isfinite(Position[i]) || std::abs(Position[i]) > 1048575.f || !std::isfinite(LinearVelocity[i]) ||
            std::abs(LinearVelocity[i]) > 65536.f || !std::isfinite(AngularVelocity[i]) || std::abs(AngularVelocity[i]) > 65536.f) return false;
    return true;
}
void RigidBodyPose::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    for (float value : Position) Write(aWriter, value);
    for (float value : Rotation) Write(aWriter, value);
    for (float value : LinearVelocity) Write(aWriter, value);
    for (float value : AngularVelocity) Write(aWriter, value);
}
bool RigidBodyPose::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    for (float& value : Position) if (!Read(aReader, value)) return false;
    for (float& value : Rotation) if (!Read(aReader, value)) return false;
    for (float& value : LinearVelocity) if (!Read(aReader, value)) return false;
    for (float& value : AngularVelocity) if (!Read(aReader, value)) return false;
    return IsValid();
}
bool RigidBodyRig::IsValid() const noexcept
{
    if (!Count || Count > kMaxBodies) return false;
    for (size_t i = 0; i < Count; ++i)
        if (!Bodies[i].Key || (i && Bodies[i - 1].Key >= Bodies[i].Key) || !Bodies[i].Pose.IsValid()) return false;
    return true;
}
bool RigidBodyRig::SameGraph(const RigidBodyRig& acOther) const noexcept
{
    if (!Count || Count > kMaxBodies || Count != acOther.Count) return false;
    for (size_t i = 0; i < Count; ++i) if (Bodies[i].Key != acOther.Bodies[i].Key) return false;
    return true;
}
bool RigidBodyRig::Predict(const RigidBodyRig& acSource, float aSeconds, RigidBodyRig& aOutput) noexcept
{
    if (!acSource.IsValid() || !std::isfinite(aSeconds) || aSeconds < 0.f || aSeconds > .1f) return false;
    RigidBodyRig output = acSource;
    for (size_t i = 0; i < output.Count; ++i)
    {
        auto& pose = output.Bodies[i].Pose;
        double speed{}, angular{};
        for (size_t j = 0; j < 3; ++j) { speed += double(pose.LinearVelocity[j]) * pose.LinearVelocity[j]; angular += double(pose.AngularVelocity[j]) * pose.AngularVelocity[j]; }
        speed = std::sqrt(speed); angular = std::sqrt(angular);
        const float t = static_cast<float>(speed > 0. ? std::min(double(aSeconds), 256. / speed) : aSeconds);
        for (size_t j = 0; j < 3; ++j) pose.Position[j] += pose.LinearVelocity[j] * t;
        if (angular > 1.e-6)
        {
            const double half = std::min(angular * aSeconds, 1.) * .5;
            const double w = std::cos(half), k = std::sin(half) / angular;
            const double x = pose.AngularVelocity[0] * k, y = pose.AngularVelocity[1] * k, z = pose.AngularVelocity[2] * k;
            const auto q = pose.Rotation;
            pose.Rotation = {static_cast<float>(w * q[0] - x * q[1] - y * q[2] - z * q[3]),
                static_cast<float>(w * q[1] + x * q[0] + y * q[3] - z * q[2]),
                static_cast<float>(w * q[2] - x * q[3] + y * q[0] + z * q[1]),
                static_cast<float>(w * q[3] + x * q[2] - y * q[1] + z * q[0])};
        }
    }
    if (!output.IsValid()) return false;
    aOutput = output; return true;
}
bool RigidBodyRig::Blend(const RigidBodyRig& acFirst, const RigidBodyRig& acSecond, float aDelta, RigidBodyRig& aOutput) noexcept
{
    if (!std::isfinite(aDelta) || !acFirst.IsValid() || !acSecond.IsValid() || !acFirst.SameGraph(acSecond)) return false;
    const float t = std::clamp(aDelta, 0.f, 1.f);
    RigidBodyRig output = acSecond;
    for (size_t i = 0; i < output.Count; ++i)
    {
        const auto& a = acFirst.Bodies[i].Pose; const auto& b = acSecond.Bodies[i].Pose;
        auto& pose = output.Bodies[i].Pose;
        double dot{}, norm{};
        for (size_t j = 0; j < 4; ++j) dot += double(a.Rotation[j]) * b.Rotation[j];
        for (size_t j = 0; j < 4; ++j)
        {
            pose.Rotation[j] = a.Rotation[j] * (1.f - t) + b.Rotation[j] * (dot < 0. ? -t : t);
            norm += double(pose.Rotation[j]) * pose.Rotation[j];
        }
        if (norm < 1.e-8) return false;
        const float inverse = static_cast<float>(1. / std::sqrt(norm));
        for (float& value : pose.Rotation) value *= inverse;
        for (size_t j = 0; j < 3; ++j)
        {
            pose.Position[j] = a.Position[j] * (1.f - t) + b.Position[j] * t;
            pose.LinearVelocity[j] = a.LinearVelocity[j] * (1.f - t) + b.LinearVelocity[j] * t;
            pose.AngularVelocity[j] = a.AngularVelocity[j] * (1.f - t) + b.AngularVelocity[j] * t;
        }
    }
    if (!output.IsValid()) return false;
    aOutput = output;
    return true;
}
void RigidBodyRig::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid() ? Count : 0, 8);
    if (!IsValid()) return;
    for (size_t i = 0; i < Count; ++i) { aWriter.WriteBits(Bodies[i].Key, 64); Bodies[i].Pose.Serialize(aWriter); }
}
bool RigidBodyRig::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    uint64_t count{};
    if (!aReader.ReadBits(count, 8) || !count || count > kMaxBodies) return false;
    Count = static_cast<uint8_t>(count);
    for (size_t i = 0; i < Count; ++i) if (!aReader.ReadBits(Bodies[i].Key, 64) || !Bodies[i].Pose.Deserialize(aReader)) return false;
    return IsValid();
}
