#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <Structs/ContentCompatibility.h>
#include <Structs/ProtocolVersion.h>
#include "../client/Systems/ContentFingerprint.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace
{
std::filesystem::path Utf8Path(const char* apText)
{
    return std::filesystem::path(reinterpret_cast<const char8_t*>(apText));
}

void PrintString(const String& acValue)
{
    std::cout << '"' << ContentCompatibility::EscapeJson(acValue).c_str() << '"';
}

void PrintDigest(const std::array<uint8_t, 32>& acDigest)
{
    static constexpr char cHex[] = "0123456789abcdef";
    std::cout << '"';
    for (uint8_t byte : acDigest)
        std::cout << cHex[byte >> 4] << cHex[byte & 15];
    std::cout << '"';
}
} // namespace

int RunAudit(int argc, char** argv)
{
    if (argc < 4 || argc > 5)
    {
        std::cerr << "Usage: TrueCoopContentAudit <Data directory> <runtime e.g. 1.7.104.0> <full-engine-loadorder.txt> [loaded-SKSE.dll]\n"
                     "The UTF-8 load order must include implicit masters and Skyrim.ccc entries, one filename per line.\n"
                     "Run in the same mod-manager VFS as the game. This read-only audit does not prove gameplay compatibility.\n";
        return 2;
    }
    const auto dataPath = Utf8Path(argv[1]);
    std::ifstream order(Utf8Path(argv[3]), std::ios::binary);
    if (!order)
    {
        std::cerr << "Cannot read engine load order\n";
        return 2;
    }
    Mods mods;
    std::string line;
    uint16_t standard = 0;
    uint16_t lite = 0;
    while (std::getline(order, line))
    {
        if (mods.ModList.empty() && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        if (line.front() == '*')
            line.erase(0, 1);
        // Refuse paths before touching the filesystem; Validate will check full plugin syntax.
        if (line.find_first_of("/\\:") != std::string::npos)
        {
            std::cerr << "Load order must contain filenames, not paths\n";
            return 2;
        }
        Mods::Entry entry;
        entry.Filename = line.c_str();
        const auto path = dataPath / Utf8Path(line.c_str());
        std::ifstream plugin(path, std::ios::binary);
        std::array<uint8_t, 24> header{};
        if (!plugin.read(reinterpret_cast<char*>(header.data()), header.size()))
        {
            std::cerr << "Cannot read plugin: " << line << '\n';
            return 1;
        }
        entry.IsLite = (header[9] & 2) != 0;
        entry.Id = entry.IsLite ? lite++ : standard++;
        mods.ModList.push_back(std::move(entry));
        if (mods.ModList.size() > 4096)
        {
            std::cerr << "Too many plugins\n";
            return 1;
        }
    }
    std::string runtime = argv[2];
    std::replace(runtime.begin(), runtime.end(), '.', '-');
    const auto addressLibrary = dataPath / "SKSE" / "Plugins" / ("versionlib-" + runtime + ".bin");
    const auto skse = argc == 5 ? Utf8Path(argv[4]) : std::filesystem::path{};
    FingerprintContent(dataPath, addressLibrary, skse, mods);
    const auto result = ContentCompatibility::Validate(mods);
    std::cout << "{\"protocol\":" << kTrueCoopProtocolVersion << ",\"runtime\":";
    PrintString(argv[2]);
    std::cout << ",\"manifestValid\":" << (result ? "true" : "false") << ",\"diagnostic\":";
    PrintString(ContentCompatibility::Describe(result));
    std::cout << ",\"gameStatus\":\"not-game-tested\",\"plugins\":[";
    bool first = true;
    for (const auto& entry : mods.ModList)
    {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << "{\"filename\":";
        PrintString(entry.Filename);
        std::cout << ",\"slot\":" << entry.Id << ",\"lite\":" << (entry.IsLite ? "true" : "false") << ",\"sha256\":";
        PrintDigest(entry.Digest);
        std::cout << ",\"masters\":[";
        for (size_t i = 0; i < entry.Masters.size(); ++i)
        {
            if (i)
                std::cout << ',';
            PrintString(entry.Masters[i]);
        }
        std::cout << "]}";
    }
    std::cout << "],\"resources\":[";
    first = true;
    for (const auto& resource : mods.Resources)
    {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << "{\"filename\":";
        PrintString(resource.Filename);
        std::cout << ",\"sha256\":";
        PrintDigest(resource.Digest);
        std::cout << '}';
    }
    std::cout << "]}\n";
    return result ? 0 : 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    Vector<std::string> arguments;
    for (int i = 0; i < argc; ++i)
    {
        const auto utf8 = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    Vector<char*> pointers;
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    return RunAudit(argc, pointers.data());
}
#else
int main(int argc, char** argv)
{
    return RunAudit(argc, argv);
}
#endif
