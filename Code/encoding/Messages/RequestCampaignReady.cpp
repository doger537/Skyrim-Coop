#include "RequestCampaignReady.h"
#include <TiltedCore/Serialization.hpp>

void RequestCampaignReady::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Nonce);
    Serialization::WriteVarInt(aWriter, Revision);
    Serialization::WriteVarInt(aWriter, SeedCount);
}

void RequestCampaignReady::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Nonce = Serialization::ReadVarInt(aReader);
    Revision = Serialization::ReadVarInt(aReader);
    const uint64_t count = Serialization::ReadVarInt(aReader);
    SeedCount = count <= 4095 ? static_cast<uint32_t>(count) : UINT32_MAX;
}
