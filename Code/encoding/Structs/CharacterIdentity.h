#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <TiltedCore/Buffer.hpp>

// A saved character identity, not an account credential or a runtime actor ID.
// Eight exact uint16 float values survive the engine's ordinary GLOB save data.
struct CharacterIdentity
{
    std::array<uint16_t, 8> Parts{};
    bool operator==(const CharacterIdentity&) const noexcept = default;
    bool IsValid() const noexcept
    {
        for (uint16_t part : Parts)
            if (part)
                return true;
        return false;
    }
    static bool FromGlobals(const std::array<float, 8>& acValues, CharacterIdentity& aResult) noexcept
    {
        CharacterIdentity result;
        for (size_t i = 0; i < result.Parts.size(); ++i)
        {
            const float value = acValues[i];
            if (!std::isfinite(value) || value < 0.f || value > 65535.f || std::floor(value) != value)
                return false;
            result.Parts[i] = static_cast<uint16_t>(value);
        }
        aResult = result;
        return true;
    }
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
    {
        for (uint16_t part : Parts)
            aWriter.WriteBits(part, 16);
    }
    bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
    {
        CharacterIdentity result;
        for (uint16_t& part : result.Parts)
        {
            uint64_t bits{};
            if (!aReader.ReadBits(bits, 16))
                return false;
            part = static_cast<uint16_t>(bits);
        }
        *this = result;
        return IsValid();
    }
};
