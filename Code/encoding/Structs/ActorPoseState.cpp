#include "ActorPoseState.h"
bool ActorPoseState::IsValid() const noexcept
{
    return Known ? SitSleep <= 8 && Knock <= 7 && Life <= 8 && (!Furniture || SitSleep != 0) :
        !Ragdoll && !SitSleep && !Knock && !Life && !Furniture && FurnitureMarker == UINT32_MAX;
}
bool ActorPoseState::IsDriven() const noexcept
{
    return Known && (Ragdoll || SitSleep || Knock || Life == 1 || Life == 2 || Life == 3 || Life == 7 || Life == 8);
}
void ActorPoseState::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Known, 1);
    if (!Known) return;
    aWriter.WriteBits(Ragdoll, 1); aWriter.WriteBits(SitSleep, 4); aWriter.WriteBits(Knock, 3); aWriter.WriteBits(Life, 4);
    aWriter.WriteBits(Furniture.ModId, 32); aWriter.WriteBits(Furniture.BaseId, 32); aWriter.WriteBits(FurnitureMarker, 32);
}
bool ActorPoseState::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    uint64_t value{};
    if (!aReader.ReadBits(value, 1)) return false;
    Known = value != 0;
    if (!Known) return true;
    if (!aReader.ReadBits(value, 1)) return false; Ragdoll = value != 0;
    if (!aReader.ReadBits(value, 4)) return false; SitSleep = static_cast<uint8_t>(value);
    if (!aReader.ReadBits(value, 3)) return false; Knock = static_cast<uint8_t>(value);
    if (!aReader.ReadBits(value, 4)) return false; Life = static_cast<uint8_t>(value);
    if (!aReader.ReadBits(value, 32)) return false; Furniture.ModId = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 32)) return false; Furniture.BaseId = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 32)) return false; FurnitureMarker = static_cast<uint32_t>(value);
    return IsValid();
}
