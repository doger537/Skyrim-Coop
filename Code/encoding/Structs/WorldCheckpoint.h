#pragma once
#include <Structs/CharacterIdentity.h>
#include <Structs/TimeModel.h>
#include <TiltedCore/Stl.hpp>
#include <array>
#include <string_view>

struct CheckpointFile
{
    static constexpr uint64_t kMaxBytes = 256 * 1024 * 1024;
    std::array<uint8_t, 32> Digest{};
    uint64_t Bytes{};
    bool IsValid(bool aOptional = false) const noexcept;
    bool operator==(const CheckpointFile&) const noexcept = default;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
struct CheckpointPlayer
{
    CharacterIdentity Character;
    CheckpointFile Save;
    CheckpointFile CoSave;
    bool IsValid() const noexcept { return Character.IsValid() && Save.IsValid() && CoSave.IsValid(true); }
    bool operator==(const CheckpointPlayer&) const noexcept = default;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
struct WorldCheckpoint
{
    uint64_t Id{};
    uint64_t SavedEpoch{};
    TimeModel Calendar;
    std::array<CheckpointPlayer, 2> Players; // Host first, guest second.
    bool Decoded{true};
    bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};

namespace CheckpointNames
{
// An opaque engine filename, never a client-selected path. Lower-case ASCII
// tokens and fixed lengths prohibit separators, traversal and alternate streams.
TiltedPhoques::String Make(std::string_view acWorld, uint64_t aId, const CharacterIdentity& acCharacter) noexcept;
bool Parse(std::string_view acName, TiltedPhoques::String& aWorld, uint64_t& aId, CharacterIdentity& aCharacter) noexcept;
}
