#pragma once
#include <array>
#include <cstdint>
#include <filesystem>

bool FingerprintPluginFile(const std::filesystem::path& acPath, std::array<uint8_t, 32>& aDigest) noexcept;
