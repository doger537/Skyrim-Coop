#pragma once
#include <cstdint>

// A fixed prefix lets a new server refuse an unknown layout before parsing its strings/manifest.
// BUILD_COMMIT remains an independent, mandatory check.
inline constexpr uint32_t kTrueCoopProtocolVersion = 0x54430003;
