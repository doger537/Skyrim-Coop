#include <Structs/WorldCheckpoint.h>
#include <algorithm>

bool CheckpointFile::IsValid(bool aOptional) const noexcept
{
    const bool nonzero = std::any_of(Digest.begin(), Digest.end(), [](uint8_t value) { return value != 0; });
    return Bytes == 0 ? aOptional && !nonzero : Bytes <= kMaxBytes && nonzero;
}
void CheckpointFile::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Bytes, 64);
    for (uint8_t byte : Digest)
        aWriter.WriteBits(byte, 8);
}
bool CheckpointFile::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    if (!aReader.ReadBits(Bytes, 64))
        return false;
    for (uint8_t& byte : Digest)
    {
        uint64_t value{};
        if (!aReader.ReadBits(value, 8))
            return false;
        byte = static_cast<uint8_t>(value);
    }
    return IsValid(true);
}
void CheckpointPlayer::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Character.Serialize(aWriter);
    Save.Serialize(aWriter);
    CoSave.Serialize(aWriter);
    Physics.Serialize(aWriter);
}
bool CheckpointPlayer::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    return Character.Deserialize(aReader) && Save.Deserialize(aReader) && CoSave.Deserialize(aReader) && Physics.Deserialize(aReader) && IsValid();
}
bool CheckpointAdmissionProof::IsValid() const noexcept
{
    return WorldId.size() == 32 && Player.IsValid() &&
           std::all_of(WorldId.begin(), WorldId.end(), [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); });
}
void CheckpointAdmissionProof::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    for (size_t i = 0; i < 32; ++i)
        aWriter.WriteBits(i < WorldId.size() ? static_cast<uint8_t>(WorldId[i]) : 0, 8);
    Player.Serialize(aWriter);
}
bool CheckpointAdmissionProof::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    std::array<char, 32> world{};
    for (char& byte : world)
    {
        uint64_t value{};
        if (!aReader.ReadBits(value, 8))
            return false;
        byte = static_cast<char>(value);
    }
    WorldId.assign(world.data(), world.size());
    return Player.Deserialize(aReader) && IsValid();
}
bool WorldCheckpoint::IsValid() const noexcept
{
    return Decoded && Id && SavedEpoch && PhysicsEntries <= 8192 && std::any_of(PhysicsDigest.begin(), PhysicsDigest.end(), [](uint8_t b) { return b != 0; }) &&
           Calendar.IsValid() && Players[0].IsValid() && Players[1].IsValid() && Players[0].Character != Players[1].Character;
}
void WorldCheckpoint::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(2, 16); // Manifest schema.
    aWriter.WriteBits(Id, 64);
    aWriter.WriteBits(SavedEpoch, 64);
    Calendar.Serialize(aWriter);
    for (auto b : PhysicsDigest)
        aWriter.WriteBits(b, 8);
    aWriter.WriteBits(PhysicsEntries, 32);
    for (const auto& player : Players)
        player.Serialize(aWriter);
}
void WorldCheckpoint::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t schema{};
    if (!aReader.ReadBits(schema, 16) || schema != 2 || !aReader.ReadBits(Id, 64) || !aReader.ReadBits(SavedEpoch, 64))
        return;
    Calendar.Deserialize(aReader);
    uint64_t value{};
    for (auto& b : PhysicsDigest)
    {
        if (!aReader.ReadBits(value, 8))
            return;
        b = static_cast<uint8_t>(value);
    }
    if (!aReader.ReadBits(value, 32) || value > 8192)
        return;
    PhysicsEntries = static_cast<uint32_t>(value);
    for (auto& player : Players)
        if (!player.Deserialize(aReader))
            return;
    Decoded = true;
    Decoded = IsValid();
}

namespace CheckpointNames
{
namespace
{
constexpr char hex[] = "0123456789abcdef";
bool Hex(char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}
uint8_t Nibble(char value) noexcept
{
    return static_cast<uint8_t>(value <= '9' ? value - '0' : value - 'a' + 10);
}
} // namespace
TiltedPhoques::String Make(std::string_view acWorld, uint64_t aId, const CharacterIdentity& acCharacter) noexcept
{
    if (acWorld.size() != 32 || !aId || !acCharacter.IsValid() || !std::all_of(acWorld.begin(), acWorld.end(), Hex))
        return {};
    TiltedPhoques::String name("TrueCoop_");
    name.append(acWorld.data(), acWorld.size());
    name += '_';
    for (int shift = 60; shift >= 0; shift -= 4)
        name += hex[(aId >> shift) & 15];
    name += '_';
    for (uint16_t part : acCharacter.Parts)
        for (int shift = 12; shift >= 0; shift -= 4)
            name += hex[(part >> shift) & 15];
    return name;
}
bool Parse(std::string_view acName, TiltedPhoques::String& aWorld, uint64_t& aId, CharacterIdentity& aCharacter) noexcept
{
    if (acName.ends_with(".ess"))
        acName.remove_suffix(4);
    if (acName.size() != 91 || !acName.starts_with("TrueCoop_") || acName[41] != '_' || acName[58] != '_')
        return false;
    for (size_t i = 9; i < acName.size(); ++i)
        if (i != 41 && i != 58 && !Hex(acName[i]))
            return false;
    uint64_t id{};
    for (size_t i = 42; i < 58; ++i)
        id = (id << 4) | Nibble(acName[i]);
    CharacterIdentity character;
    for (size_t i = 0; i < character.Parts.size(); ++i)
        for (size_t digit = 0; digit < 4; ++digit)
            character.Parts[i] = static_cast<uint16_t>((character.Parts[i] << 4) | Nibble(acName[59 + i * 4 + digit]));
    if (!id || !character.IsValid())
        return false;
    aWorld.assign(acName.data() + 9, 32);
    aId = id;
    aCharacter = character;
    return true;
}
} // namespace CheckpointNames
