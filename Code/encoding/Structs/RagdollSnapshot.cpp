#include "RagdollSnapshot.h"
void RagdollSnapshot::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(ServerId, 32); aWriter.WriteBits(OwnershipEpoch, 32); aWriter.WriteBits(Sequence, 64); aWriter.WriteBits(Tick, 64);
    aWriter.WriteBits(Interaction, 1);
    for (const auto& id : {Cell, WorldSpace}) { aWriter.WriteBits(id.ModId, 32); aWriter.WriteBits(id.BaseId, 32); }
    Rig.Serialize(aWriter);
}
bool RagdollSnapshot::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {}; Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 32)) return false; ServerId = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 32)) return false; OwnershipEpoch = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(Sequence, 64) || !aReader.ReadBits(Tick, 64)) return false;
    if (!aReader.ReadBits(value, 1)) return false; Interaction = value != 0;
    for (auto* id : {&Cell, &WorldSpace})
    {
        if (!aReader.ReadBits(value, 32)) return false; id->ModId = static_cast<uint32_t>(value);
        if (!aReader.ReadBits(value, 32)) return false; id->BaseId = static_cast<uint32_t>(value);
    }
    if (!Rig.Deserialize(aReader)) return false;
    Decoded = true; Decoded = IsValid();
    return Decoded;
}
