#include "NotifyCampaignCheckpoint.h"
#include <TiltedCore/Serialization.hpp>

void NotifyCampaignCheckpoint::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(State, 8);
    Serialization::WriteString(aWriter, WorldId);
    Serialization::WriteVarInt(aWriter, Nonce);
    Serialization::WriteVarInt(aWriter, Revision);
    Serialization::WriteVarInt(aWriter, Count);
    Serialization::WriteBool(aWriter, SeedOwner);
    Serialization::WriteBool(aWriter, BaselinePending);
    Serialization::WriteBool(aWriter, AllowMisc);
    aWriter.WriteBits(MiscQuests.size(), 9);
    for (const auto& id : MiscQuests)
        id.Serialize(aWriter);
}

void NotifyCampaignCheckpoint::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t data = 0;
    aReader.ReadBits(data, 8);
    State = static_cast<uint8_t>(data);
    WorldId = Serialization::ReadString(aReader);
    Nonce = Serialization::ReadVarInt(aReader);
    Revision = Serialization::ReadVarInt(aReader);
    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > 4095 || State > Accepted || WorldId.size() != 32)
    {
        Decoded = false;
        return;
    }
    Count = static_cast<uint32_t>(count);
    SeedOwner = Serialization::ReadBool(aReader);
    BaselinePending = Serialization::ReadBool(aReader);
    AllowMisc = Serialization::ReadBool(aReader);
    aReader.ReadBits(data, 9);
    if (data > 256)
    {
        Decoded = false;
        return;
    }
    MiscQuests.resize(data);
    for (auto& id : MiscQuests)
        id.Deserialize(aReader);
}
