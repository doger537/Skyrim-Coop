#include "ContentFingerprint.h"
#include <cryptopp/sha.h>
#include <fstream>

bool FingerprintPluginFile(const std::filesystem::path& acPath, std::array<uint8_t, 32>& aDigest) noexcept
{
    aDigest.fill(0);
    std::error_code ec;
    const auto before = std::filesystem::last_write_time(acPath, ec);
    if (ec)
        return false;
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return false;
    CryptoPP::SHA256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (file)
    {
        file.read(buffer.data(), buffer.size());
        hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()), static_cast<size_t>(file.gcount()));
    }
    if (!file.eof() || before != std::filesystem::last_write_time(acPath, ec) || ec)
        return false;
    hash.Final(aDigest.data());
    return true;
}
