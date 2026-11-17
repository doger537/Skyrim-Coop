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
// Exactly the bundled 6420-byte ESP -> the deterministic 7530-byte additive
// marker ESP. All existing record bytes are preserved by the asset builder.
inline constexpr auto kPrevious = Digest("3196266aa29855574bda48335da9ab2f18ad33a80eee116400da7c5c81c93d7e");
inline constexpr auto kMarkers = Digest("9ef8a8f8bb08dca12f43355e073adf0a3bd69cb4f947964fec10a278a1f7694b");
bool Upgrade(Mods& aPinnedContent) noexcept;
}
