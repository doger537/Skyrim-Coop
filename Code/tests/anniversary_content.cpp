#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <catch2/catch.hpp>
#include <Structs/AnniversaryContent.h>
#include <Structs/ContentCompatibility.h>
#include <Messages/AuthenticationRequest.h>
#include "../client/Systems/ContentFingerprint.h"
#include <fstream>
#include <set>

namespace
{
Mods AnniversaryManifest()
{
    Mods mods;
    uint16_t standard = 0;
    uint16_t lite = 0;
    for (const char* name : {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"})
        mods.ModList.push_back({name, standard++, false});
    for (const auto name : kAnniversaryPlugins)
    {
        const bool cLite = name.ends_with(".esl");
        auto& entry = mods.ModList.emplace_back();
        entry.Filename = String(name.data(), name.size());
        entry.Id = cLite ? lite++ : standard++;
        entry.IsLite = cLite;
        entry.Masters = {"Skyrim.esm", "Update.esm"}; // synthetic, not claims about actual MASTs
        auto& archive = mods.Resources.emplace_back();
        archive.Filename = "data/" + entry.Filename.substr(0, entry.Filename.size() - 4) + ".bsa";
    }
    for (const char* name : {"runtime/address-library", "data/loose-files"})
        mods.Resources.push_back({name});
    for (auto& entry : mods.ModList)
        entry.Digest.fill(42);
    for (auto& resource : mods.Resources)
        resource.Digest.fill(43);
    return mods;
}

struct TempProfile
{
    std::filesystem::path Path = std::filesystem::current_path() / "ae-content-test";
    TempProfile()
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(Path, error));
    }
    ~TempProfile()
    {
        std::error_code error;
        std::filesystem::remove_all(Path, error);
    }
    void Write(const std::filesystem::path& acName, const std::string& acBytes)
    {
        std::error_code error;
        std::filesystem::create_directories((Path / acName).parent_path(), error);
        REQUIRE_FALSE(error);
        std::ofstream file(Path / acName, std::ios::binary);
        REQUIRE(static_cast<bool>(file.write(acBytes.data(), acBytes.size())));
    }
};

std::string PluginHeader(bool aLite, const std::string& acMaster = {})
{
    std::string data;
    if (!acMaster.empty())
    {
        data = "MAST";
        const size_t length = acMaster.size() + 1;
        data += static_cast<char>(length);
        data += static_cast<char>(length >> 8);
        data += acMaster;
        data += '\0';
    }
    std::string header(24, '\0');
    header.replace(0, 4, "TES4");
    const uint32_t length = static_cast<uint32_t>(data.size());
    for (size_t i = 0; i < 4; ++i)
        header[4 + i] = static_cast<char>(length >> (8 * i));
    if (aLite)
        header[9] = 2;
    return header + data;
}
} // namespace

TEST_CASE("The complete 74-Creation profile fits and survives authentication encoding", "[content.ae]")
{
    const auto mods = AnniversaryManifest();
    REQUIRE(static_cast<bool>(ContentCompatibility::Validate(mods)));
    std::set<std::string_view> unique(kAnniversaryPlugins.begin(), kAnniversaryPlugins.end());
    REQUIRE(unique.size() == 74);
    AuthenticationRequest request;
    request.RuntimeVersion = "1.7.104.0";
    request.UserMods = mods;
    request.Token = "test-password";
    TiltedPhoques::Buffer buffer(65536);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    REQUIRE(writer.Size() < ContentCompatibility::kMaxManifestBytes);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    uint64_t opcode{};
    reader.ReadBits(opcode, 8);
    AuthenticationRequest decoded;
    decoded.DeserializeRaw(reader);
    REQUIRE(request == decoded);
    REQUIRE(static_cast<bool>(ContentCompatibility::Compare(mods, decoded.UserMods)));
}

TEST_CASE("Every Anniversary plugin and archive is checked independently", "[content.ae]")
{
    using namespace ContentCompatibility;
    const auto expected = AnniversaryManifest();
    for (size_t i = 5; i < expected.ModList.size(); ++i)
    {
        INFO(expected.ModList[i].Filename.c_str());
        auto changed = expected;
        changed.ModList[i].Digest[0] ^= 1;
        REQUIRE(Compare(expected, changed).Reason == Issue::DifferentPlugin);
        changed = expected;
        changed.ModList.erase(changed.ModList.begin() + i);
        REQUIRE(Compare(expected, changed).Reason == Issue::MissingPlugin);
        changed = expected;
        changed.Resources[i - 5].Digest[0] ^= 1;
        REQUIRE(Compare(expected, changed).Reason == Issue::DifferentResource);
        changed = expected;
        changed.Resources.erase(changed.Resources.begin() + i - 5);
        REQUIRE(Validate(changed).Reason == Issue::MissingResource);
    }
}

TEST_CASE("SE and AE ownership labels cannot authorize mismatched active content", "[content.limited]")
{
    using namespace ContentCompatibility;
    const auto ae = AnniversaryManifest();
    auto common = ae;
    common.ModList.resize(5);
    common.Resources.erase(common.Resources.begin(), common.Resources.begin() + 74);
    REQUIRE(static_cast<bool>(Validate(common)));
    REQUIRE(Compare(ae, common).Reason == Issue::MissingPlugin);
    REQUIRE(Compare(common, ae).Reason == Issue::ExtraPlugin);
    // Equal active content is permitted regardless of an unverified ownership label.
    REQUIRE(static_cast<bool>(Compare(common, common)));
    auto differentLibrary = common;
    differentLibrary.Resources[0].Digest[0] ^= 1;
    REQUIRE(Compare(common, differentLibrary).Reason == Issue::DifferentResource);
}

TEST_CASE("Masters and resources fail closed with actionable diagnostics", "[content.ae]")
{
    using namespace ContentCompatibility;
    const auto expected = AnniversaryManifest();
    auto changed = expected;
    SECTION("Missing transitive master")
    {
        changed.ModList[5].Masters.push_back("missing.esm");
        const auto result = Validate(changed);
        REQUIRE(result.Reason == Issue::MissingDependency);
        REQUIRE(result.Filename.find("missing.esm") != String::npos);
        REQUIRE(Describe(result).find("Install and enable") != String::npos);
    }
    SECTION("Master loads after dependent")
    {
        changed.ModList[5].Masters.push_back(changed.ModList[6].Filename);
        REQUIRE(Validate(changed).Reason == Issue::DifferentOrder);
    }
    SECTION("Master cycle and duplicate masters")
    {
        changed.ModList[5].Masters.push_back(changed.ModList[5].Filename);
        REQUIRE(Validate(changed).Reason == Issue::InvalidManifest);
        changed = expected;
        changed.ModList[5].Masters.push_back("SKYRIM.ESM");
        REQUIRE(Validate(changed).Reason == Issue::InvalidManifest);
    }
    SECTION("Same plugin digest but inconsistent reported masters")
    {
        changed.ModList[5].Masters.pop_back();
        REQUIRE(Compare(expected, changed).Reason == Issue::DifferentPlugin);
    }
    SECTION("Loose scripts or animations differ")
    {
        changed.Resources.back().Digest[0] ^= 1;
        REQUIRE(Compare(expected, changed).Reason == Issue::DifferentResource);
    }
    SECTION("Missing or unreadable Address Library")
    {
        changed.Resources[74].Digest.fill(0);
        REQUIRE(Validate(changed).Reason == Issue::UnreadableResource);
        changed.Resources.erase(changed.Resources.begin() + 74);
        REQUIRE(Validate(changed).Reason == Issue::MissingResource);
    }
    SECTION("Extra native DLL")
    {
        changed.Resources.push_back({"runtime/skse"});
        changed.Resources.back().Digest.fill(44);
        REQUIRE(Compare(expected, changed).Reason == Issue::ExtraResource);
    }
    SECTION("Resource path traversal and duplicate paths")
    {
        changed.Resources.back().Filename = "data/../escape";
        REQUIRE(Validate(changed).Reason == Issue::InvalidManifest);
        changed = expected;
        changed.Resources.push_back(changed.Resources.front());
        REQUIRE(Validate(changed).Reason == Issue::InvalidManifest);
    }
    SECTION("Resource names are case insensitive and order independent")
    {
        changed.Resources.back().Filename = "DATA/LOOSE-FILES";
        std::reverse(changed.Resources.begin(), changed.Resources.end());
        REQUIRE(static_cast<bool>(Compare(expected, changed)));
    }
}

TEST_CASE("Unknown authentication protocol is refused before parsing the changed layout", "[content.protocol]")
{
    TiltedPhoques::Buffer buffer(32);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(kTrueCoopProtocolVersion + 1, 32);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    AuthenticationRequest request;
    request.DeserializeRaw(reader);
    REQUIRE(request.ProtocolVersion != kTrueCoopProtocolVersion);
    REQUIRE(request.UserMods.ModList.empty());
    REQUIRE(request.Token.empty());
}

TEST_CASE("TES4 master parsing rejects truncated or inconsistent ESL headers", "[content.files]")
{
    TempProfile profile;
    const auto file = profile.Path / "test.esl";
    Vector<String> masters;
    profile.Write("test.esl", PluginHeader(true, "Skyrim.esm"));
    REQUIRE(ReadPluginMasters(file, true, masters));
    REQUIRE(masters == Vector<String>{"Skyrim.esm"});
    REQUIRE_FALSE(ReadPluginMasters(file, false, masters));
    auto truncated = PluginHeader(true, "Skyrim.esm");
    truncated.pop_back();
    profile.Write("test.esl", truncated);
    REQUIRE_FALSE(ReadPluginMasters(file, true, masters));
    profile.Write("test.esl", "not a plugin");
    REQUIRE_FALSE(ReadPluginMasters(file, true, masters));
    auto invalid = PluginHeader(true, "Skyrim.esm");
    invalid.back() = 'x';
    profile.Write("test.esl", invalid);
    REQUIRE_FALSE(ReadPluginMasters(file, true, masters));
}

TEST_CASE("Fingerprinting detects BSA, loose asset and native library changes", "[content.files]")
{
    TempProfile profile;
    profile.Write("Skyrim.esm", PluginHeader(false));
    profile.Write("ccBGSSSE037-Curios.esl", PluginHeader(true, "Skyrim.esm"));
    profile.Write("ccBGSSSE037-Curios.bsa", "archive");
    profile.Write("scripts/quest.pex", "script");
    profile.Write("meshes/actor.hkx", "animation");
    profile.Write("SKSE/Plugins/library.dll", "native plugin");
    profile.Write("SKSE/Plugins/versionlib-test.bin", "addresses");
    profile.Write("skse.dll", "script extender");
    Mods mods;
    mods.ModList = {{"Skyrim.esm", 0, false}, {"ccBGSSSE037-Curios.esl", 0, true}};
    const auto collect = [&]
    {
        return FingerprintContent(profile.Path, profile.Path / "SKSE/Plugins/versionlib-test.bin", profile.Path / "skse.dll", mods);
    };
    REQUIRE(collect());
    REQUIRE(static_cast<bool>(ContentCompatibility::Validate(mods)));
    const auto expected = mods;
    for (const char* name : {"ccBGSSSE037-Curios.bsa", "scripts/quest.pex", "meshes/actor.hkx", "SKSE/Plugins/library.dll", "skse.dll"})
    {
        INFO(name);
        const auto before = mods;
        profile.Write(name, "changed");
        REQUIRE(collect());
        REQUIRE(ContentCompatibility::Compare(before, mods).Reason == ContentCompatibility::Issue::DifferentResource);
    }
    std::error_code error;
    std::filesystem::remove(profile.Path / "ccBGSSSE037-Curios.bsa", error);
    REQUIRE(collect());
    REQUIRE(ContentCompatibility::Validate(mods).Reason == ContentCompatibility::Issue::MissingResource);
    std::filesystem::remove(profile.Path / "SKSE/Plugins/versionlib-test.bin", error);
    REQUIRE_FALSE(collect());
    REQUIRE(ContentCompatibility::Validate(mods).Reason == ContentCompatibility::Issue::UnreadableResource);
}
