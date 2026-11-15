#include <Messages/AuthenticationRequest.h>

void AuthenticationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(ProtocolVersion, 32);
    Serialization::WriteVarInt(aWriter, DiscordId);
    Serialization::WriteBool(aWriter, SKSEActive);
    Serialization::WriteBool(aWriter, MO2Active);
    Serialization::WriteString(aWriter, Token);
    Serialization::WriteString(aWriter, Version);
    Serialization::WriteString(aWriter, RuntimeVersion);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
}

void AuthenticationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t protocol = 0;
    aReader.ReadBits(protocol, 32);
    ProtocolVersion = static_cast<uint32_t>(protocol);
    if (ProtocolVersion != kTrueCoopProtocolVersion)
        return;

    DiscordId = Serialization::ReadVarInt(aReader);
    SKSEActive = Serialization::ReadBool(aReader);
    MO2Active = Serialization::ReadBool(aReader);
    Token = Serialization::ReadString(aReader);
    Version = Serialization::ReadString(aReader);
    RuntimeVersion = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
}
