#include <Structs/Mods.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool Mods::operator==(const Mods& acRhs) const noexcept
{
    return ModList == acRhs.ModList;
}

bool Mods::operator!=(const Mods& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Mods::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const uint16_t modCount = std::min(ModList.size(), size_t(4096)) & 0xFFFF;
    aWriter.WriteBits(modCount, 13);

    for (size_t i = 0; i < modCount; ++i)
    {
        const auto& entry = ModList[i];
        aWriter.WriteBits(entry.Id, 16);
        Serialization::WriteBool(aWriter, entry.IsLite);
        Serialization::WriteString(aWriter, entry.Filename);
        for (uint8_t byte : entry.Digest)
            aWriter.WriteBits(byte, 8);
    }
}

void Mods::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t data = 0;
    aReader.ReadBits(data, 13);

    const size_t modCount = data & 0xFFFF;
    ModList.resize(modCount);
    for (size_t i = 0; i < modCount; ++i)
    {
        aReader.ReadBits(data, 16);
        ModList[i].Id = data & 0xFFFF;
        ModList[i].IsLite = Serialization::ReadBool(aReader);
        ModList[i].Filename = Serialization::ReadString(aReader);
        for (uint8_t& byte : ModList[i].Digest)
        {
            aReader.ReadBits(data, 8);
            byte = static_cast<uint8_t>(data);
        }
    }
}
