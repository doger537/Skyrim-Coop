#pragma once
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/Mods.h>
#include <array>
#include <string_view>

namespace CorePluginUpgrade
{
constexpr std::array<uint8_t, 32> Digest(std::string_view aHex) noexcept
{
    std::array<uint8_t, 32> result{};
    if (aHex.size() != result.size() * 2)
        return result;
    const auto nibble = [](char c) constexpr -> uint8_t { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<uint8_t>((nibble(aHex[i * 2]) << 4) | nibble(aHex[i * 2 + 1]));
    return result;
}
// Only these exact bundled hashes can advance to their additive successor. The builders
// preserve all old record bytes; arbitrary resource changes never migrate.
inline constexpr auto kPrevious = Digest("3196266aa29855574bda48335da9ab2f18ad33a80eee116400da7c5c81c93d7e");
inline constexpr auto kMarkers = Digest("9ef8a8f8bb08dca12f43355e073adf0a3bd69cb4f947964fec10a278a1f7694b");
// Adds only nine nonconstant identity GLOB records; retains every old record.
inline constexpr auto kCharacters = Digest("ff52f44a06d57bded74a2a306966844845ec254d8b9ae015b30ec493ff224502");
inline constexpr auto kCheckpoints = Digest("c16976f0eff6490c17bc53d2ef5c7651250b4c9de9dc525f9261d913aa90806e");
inline constexpr auto kGrantCursor = Digest("0845abf539d68f014643f16e43dc3a59ceb411990b2e14c6aeed123741bfaf8c");
inline constexpr auto kItemRegistry = Digest("d16f162dc8f5b8ccac0b0cdef2d052f3e7db31616708e3d54690894c122f79c4");
bool Upgrade(Mods& aPinnedContent) noexcept;
}
