#include "RequestCampaignSeed.h"
#include <TiltedCore/Serialization.hpp>

void RequestCampaignSeed::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Nonce);
    Data.Serialize(aWriter);
}

void RequestCampaignSeed::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Nonce = Serialization::ReadVarInt(aReader);
    Data.Deserialize(aReader);
}
