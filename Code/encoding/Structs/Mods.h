#pragma once

#include <cstdint>
#include <array>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

struct Mods
{
    struct Entry
    {
        String Filename;
        uint16_t Id;
        bool IsLite{};
        std::array<uint8_t, 32> Digest{};

        bool operator==(const Entry& acRhs) const noexcept { return Filename == acRhs.Filename && Id == acRhs.Id && IsLite == acRhs.IsLite && Digest == acRhs.Digest; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    Vector<Entry> ModList{};

    Mods() = default;
    ~Mods() = default;

    bool operator==(const Mods& acRhs) const noexcept;
    bool operator!=(const Mods& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
