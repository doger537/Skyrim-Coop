#include <Messages/NotifyActorTeleport.h>

void NotifyActorTeleport::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, ServerId);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    FormId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Position.Serialize(aWriter);
}

void NotifyActorTeleport::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FormId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Position.Deserialize(aReader);
}
