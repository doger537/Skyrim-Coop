#include "ContentFingerprint.h"
#include <cryptopp/sha.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <Structs/ContentCompatibility.h>

bool FingerprintPluginFile(const std::filesystem::path& acPath, std::array<uint8_t, 32>& aDigest) noexcept
{
    aDigest.fill(0);
    std::error_code ec;
    const auto before = std::filesystem::last_write_time(acPath, ec);
    if (ec)
        return false;
    const uintmax_t sizeBefore = std::filesystem::file_size(acPath, ec);
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
    if (!file.eof() || before != std::filesystem::last_write_time(acPath, ec) || ec || sizeBefore != std::filesystem::file_size(acPath, ec) || ec)
        return false;
    hash.Final(aDigest.data());
    return true;
}

namespace
{
uint32_t Read32(const uint8_t* apBytes) noexcept
{
    return uint32_t(apBytes[0]) | (uint32_t(apBytes[1]) << 8) | (uint32_t(apBytes[2]) << 16) | (uint32_t(apBytes[3]) << 24);
}

String PathName(const std::filesystem::path& acPath) noexcept
{
    const auto utf8 = acPath.generic_u8string();
    return ContentCompatibility::NormalizeName(String(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}

bool IsLooseResource(const String& acName) noexcept
{
    for (const char* root : {"scripts/", "meshes/", "textures/", "sound/", "seq/", "strings/", "interface/"})
        if (acName.starts_with(root))
            return true;
    return acName.starts_with("skse/plugins/") && (acName.ends_with(".dll") || acName.ends_with(".ini") || acName.ends_with(".toml") || acName.ends_with(".json"));
}
} // namespace

bool ReadPluginMasters(const std::filesystem::path& acPath, bool aExpectedLite, Vector<String>& aMasters) noexcept
{
    aMasters.clear();
    std::ifstream file(acPath, std::ios::binary);
    std::array<uint8_t, 24> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) || std::memcmp(header.data(), "TES4", 4) != 0)
        return false;
    const uint32_t size = Read32(header.data() + 4);
    const uint32_t flags = Read32(header.data() + 8);
    if (size > 1024 * 1024 || ((flags & 0x200) != 0) != aExpectedLite)
        return false;
    Vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size()))
        return false;
    // TES4 MAST subrecords are short names. Extended XXXX subrecords are rejected rather
    // than guessing a header layout. Ordinary records after TES4 are never parsed here.
    for (size_t offset = 0; offset < data.size();)
    {
        if (data.size() - offset < 6 || std::memcmp(data.data() + offset, "XXXX", 4) == 0)
            return false;
        const uint16_t length = uint16_t(data[offset + 4]) | (uint16_t(data[offset + 5]) << 8);
        const bool cMaster = std::memcmp(data.data() + offset, "MAST", 4) == 0;
        offset += 6;
        if (length > data.size() - offset)
            return false;
        if (cMaster)
        {
            if (length < 2 || length > 256 || data[offset + length - 1] != 0 ||
                std::find(data.begin() + offset, data.begin() + offset + length - 1, 0) != data.begin() + offset + length - 1 || aMasters.size() >= 254)
                return false;
            aMasters.emplace_back(reinterpret_cast<const char*>(data.data() + offset), length - 1);
        }
        offset += length;
    }
    return true;
}

bool FingerprintContent(const std::filesystem::path& acDataPath, const std::filesystem::path& acAddressLibrary, const std::filesystem::path& acScriptExtender, Mods& aMods) noexcept
{
    aMods.Resources.clear();
    bool success = true;
    for (auto& entry : aMods.ModList)
    {
        const std::u8string filename(reinterpret_cast<const char8_t*>(entry.Filename.data()), entry.Filename.size());
        const auto path = acDataPath / filename;
        if (!ReadPluginMasters(path, entry.IsLite, entry.Masters) || !FingerprintPluginFile(path, entry.Digest))
        {
            entry.Digest.fill(0);
            success = false;
        }
    }
    const auto addFile = [&](const String& acName, const std::filesystem::path& acPath)
    {
        auto& resource = aMods.Resources.emplace_back();
        resource.Filename = acName;
        if (!FingerprintPluginFile(acPath, resource.Digest))
            success = false;
    };
    addFile("runtime/address-library", acAddressLibrary);
    if (!acScriptExtender.empty())
        addFile("runtime/skse", acScriptExtender);

    struct File
    {
        String Name;
        std::filesystem::path Path;
    };
    Vector<File> looseFiles;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(acDataPath, std::filesystem::directory_options::follow_directory_symlink, ec), end;
    bool complete = !ec;
    size_t fileCount = 0;
    while (complete && iterator != end)
    {
        const auto& path = iterator->path();
        const auto relative = path.lexically_relative(acDataPath);
        const auto name = PathName(relative);
        const bool cFile = iterator->is_regular_file(ec);
        if (ec || ++fileCount > 200000)
        {
            complete = false;
            break;
        }
        if (cFile)
        {
            if (relative.parent_path().empty() && name.ends_with(".bsa"))
                addFile("data/" + name, path);
            else if (IsLooseResource(name))
                looseFiles.push_back({name, path});
        }
        iterator.increment(ec);
        if (ec)
            complete = false;
    }
    std::sort(looseFiles.begin(), looseFiles.end(), [](const auto& left, const auto& right) { return left.Name < right.Name; });
    CryptoPP::SHA256 looseHash;
    String previous;
    for (const auto& file : looseFiles)
    {
        std::array<uint8_t, 32> digest{};
        if (file.Name == previous || !FingerprintPluginFile(file.Path, digest))
        {
            complete = false;
            break;
        }
        previous = file.Name;
        const uint32_t size = static_cast<uint32_t>(file.Name.size());
        const std::array<uint8_t, 4> length{static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 24)};
        looseHash.Update(length.data(), length.size());
        looseHash.Update(reinterpret_cast<const uint8_t*>(file.Name.data()), file.Name.size());
        looseHash.Update(digest.data(), digest.size());
    }
    auto& loose = aMods.Resources.emplace_back();
    loose.Filename = "data/loose-files";
    if (complete)
        looseHash.Final(loose.Digest.data());
    else
        success = false;
    std::sort(aMods.Resources.begin(), aMods.Resources.end(), [](const auto& left, const auto& right) { return left.Filename < right.Filename; });
    return success;
}
