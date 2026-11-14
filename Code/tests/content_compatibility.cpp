#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <catch2/catch.hpp>
#include <Structs/ContentCompatibility.h>
#include <Structs/FormIdMapping.h>
#include <Structs/OperationWindow.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/RequestInventoryChanges.h>
#include "../client/Systems/ContentFingerprint.h"
#include <algorithm>
#include <fstream>

namespace
{
Mods MakeManifest()
{
    Mods result;
    result.ModList.push_back({"Skyrim.esm", 0, false});
    result.ModList.push_back({"Update.esm", 1, false});
    result.ModList.push_back({"ccExample.esl", 0, true});
    for (auto& entry : result.ModList)
        entry.Digest.fill(static_cast<uint8_t>(entry.Filename.size()));
    for (const char* name : {"runtime/address-library", "data/loose-files"})
    {
        auto& resource = result.Resources.emplace_back();
        resource.Filename = name;
        resource.Digest.fill(42);
    }
    return result;
}

template <class T> void CheckRoundTrip(const T& acMessage)
{
    TiltedPhoques::Buffer buffer(65536);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    uint64_t opcode;
    reader.ReadBits(opcode, 8);
    T decoded;
    decoded.DeserializeRaw(reader);
    REQUIRE(decoded == acMessage);
}
} // namespace

TEST_CASE("Active content compatibility is explicit", "[content]")
{
    const auto expected = MakeManifest();
    auto actual = expected;
    using namespace ContentCompatibility;
    SECTION("Identical content")
    {
        REQUIRE(static_cast<bool>(Compare(expected, actual)));
    }
    SECTION("Filename case is insignificant")
    {
        actual.ModList[0].Filename = "SKYRIM.ESM";
        REQUIRE(static_cast<bool>(Compare(expected, actual)));
    }
    SECTION("Numeric local IDs are mapped separately")
    {
        actual.ModList[2].Id = 17;
        REQUIRE(static_cast<bool>(Compare(expected, actual)));
    }
    SECTION("Missing DLC is diagnosed")
    {
        actual.ModList.pop_back();
        const auto result = Compare(expected, actual);
        REQUIRE(result.Reason == Issue::MissingPlugin);
        REQUIRE(result.Filename == "ccExample.esl");
    }
    SECTION("Extra DLC is diagnosed")
    {
        actual.ModList.push_back({"ccOther.esl", 1, true});
        actual.ModList.back().Digest.fill(1);
        REQUIRE(Compare(expected, actual).Reason == Issue::ExtraPlugin);
    }
    SECTION("Same filename different bytes is incompatible")
    {
        actual.ModList[0].Digest[17] ^= 1;
        REQUIRE(Compare(expected, actual).Reason == Issue::DifferentPlugin);
    }
    SECTION("Same files different order is incompatible")
    {
        std::swap(actual.ModList[1], actual.ModList[2]);
        REQUIRE(Compare(expected, actual).Reason == Issue::DifferentOrder);
    }
    SECTION("ESL flag changes are incompatible")
    {
        actual.ModList[2].IsLite = false;
        actual.ModList[2].Id = 2;
        REQUIRE(Compare(expected, actual).Reason == Issue::DifferentPlugin);
    }
    SECTION("Duplicate case-insensitive names are invalid")
    {
        actual.ModList[1].Filename = "SKYRIM.ESM";
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Duplicate slots are invalid")
    {
        actual.ModList[1].Id = 0;
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Missing hashes fail closed")
    {
        actual.ModList[0].Digest.fill(0);
        REQUIRE(Validate(actual).Reason == Issue::UnreadablePlugin);
    }
    SECTION("Path traversal and control characters are invalid")
    {
        actual.ModList[0].Filename = "../Skyrim.esm";
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
        actual.ModList[0].Filename = "bad\nname.esm";
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Invalid slots are rejected")
    {
        actual.ModList[0].Id = 0xFF;
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
        actual = expected;
        actual.ModList[2].Id = 0x1000;
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Manifest fits the transport budget")
    {
        for (uint16_t i = 0; i < 1000; ++i)
        {
            actual.ModList.push_back({TiltedPhoques::String(100, 'a') + std::to_string(i).c_str() + ".esl", static_cast<uint16_t>(i + 1), true});
            actual.ModList.back().Digest.fill(1);
        }
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Empty manifest is invalid")
    {
        actual.ModList.clear();
        REQUIRE(Validate(actual).Reason == Issue::InvalidManifest);
    }
    SECTION("Diagnostics cannot break JSON")
    {
        REQUIRE(EscapeJson("a\"b\\c\n") == "a\\\"b\\\\c");
    }
}

TEST_CASE("Initial location resolves through the manifest", "[content.mapping]")
{
    const auto local = MakeManifest();
    auto server = local;
    server.ModList[0].Id = 22;
    server.ModList[2].Id = 44;
    GameId wire{};
    GameId resolved{};
    REQUIRE(ContentCompatibility::EncodeInitialForm(0xFE000ABC, local, wire));
    REQUIRE(wire.ModId == 3);
    REQUIRE(ContentCompatibility::ResolveInitialForm(wire, local, server, resolved));
    REQUIRE(resolved == GameId(44, 0xABC));
    REQUIRE(ContentCompatibility::EncodeInitialForm(0x123456, local, wire));
    REQUIRE(ContentCompatibility::ResolveInitialForm(wire, local, server, resolved));
    REQUIRE(resolved == GameId(22, 0x123456));
    REQUIRE_FALSE(ContentCompatibility::EncodeInitialForm(0xEE000001, local, wire));
    REQUIRE(ContentCompatibility::EncodeInitialForm(0, local, wire));
    REQUIRE(ContentCompatibility::ResolveInitialForm(wire, local, server, resolved));
    REQUIRE_FALSE(static_cast<bool>(resolved));
    REQUIRE_FALSE(ContentCompatibility::ResolveInitialForm(GameId(999, 1), local, server, resolved));
    REQUIRE_FALSE(ContentCompatibility::ResolveInitialForm(GameId(3, 0x1000), local, server, resolved));
    REQUIRE_FALSE(ContentCompatibility::ResolveInitialForm(GameId(0, 5), local, server, resolved));
    server.ModList.pop_back();
    REQUIRE_FALSE(ContentCompatibility::ResolveInitialForm(GameId(3, 1), local, server, resolved));
}

TEST_CASE("Unmapped standard plugins never alias the base game", "[content.mapping]")
{
    StandardModMapping map;
    uint32_t id = 999;
    REQUIRE_FALSE(map.Resolve(5, id));
    REQUIRE(id == 999);
    REQUIRE(map.Add(0, 0));
    REQUIRE(map.Resolve(0, id));
    REQUIRE(id == 0);
    REQUIRE(map.Add(5, 19));
    REQUIRE(map.Resolve(5, id));
    REQUIRE(id == 19);
    REQUIRE_FALSE(map.Resolve(6, id));
    REQUIRE_FALSE(map.Add(0xFE, 1));
    REQUIRE_FALSE(map.Resolve(0xFE, id));
    REQUIRE(map.Resolve(0xFF, id));
    REQUIRE(id == UINT32_MAX);
    map.Reset();
    REQUIRE_FALSE(map.Resolve(5, id));
    REQUIRE_FALSE(map.Resolve(0, id));
}

TEST_CASE("Inventory operations are applied at most once per connection", "[inventory.operations]")
{
    OperationWindow window;
    REQUIRE_FALSE(window.Accept(0));
    REQUIRE(window.Accept(10));
    REQUIRE_FALSE(window.Accept(10));
    REQUIRE(window.Accept(12));
    REQUIRE(window.Accept(11));
    REQUIRE_FALSE(window.Accept(11));
    REQUIRE(window.Accept(1000));
    REQUIRE_FALSE(window.Accept(12));
    REQUIRE(window.Accept(999));
    REQUIRE_FALSE(window.Accept(999));
    REQUIRE(window.Accept(UINT64_MAX));
    REQUIRE_FALSE(window.Accept(UINT64_MAX));
    REQUIRE_FALSE(window.Accept(1));
    OperationWindow reconnected;
    REQUIRE(reconnected.Accept(1));
}

TEST_CASE("Content metadata and operation IDs survive the wire", "[content.protocol]")
{
    AuthenticationRequest request;
    request.RuntimeVersion = "1.7.104.0";
    request.UserMods = MakeManifest();
    CheckRoundTrip(request);
    AuthenticationResponse response;
    response.Type = AuthenticationResponse::ResponseType::kContentMismatch;
    response.ContentError = "Different contents: ccExample.esl";
    response.UserMods = MakeManifest();
    CheckRoundTrip(response);
    RequestInventoryChanges change;
    change.OperationId = UINT64_MAX - 1;
    change.ServerId = 7;
    change.OwnershipEpoch = 2;
    change.Item.Count = -1;
    change.Item.BaseId = GameId(0, 0xF);
    CheckRoundTrip(change);
}

TEST_CASE("Plugin file fingerprint is SHA256 and unreadable files clear it", "[content.files]")
{
    std::error_code ec;
    const auto directory = std::filesystem::current_path() / "content-fingerprint-test";
    REQUIRE(std::filesystem::create_directory(directory, ec));
    const auto file = directory / "test.esp";
    struct Cleanup
    {
        std::filesystem::path File;
        std::filesystem::path Directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove(File, error);
            std::filesystem::remove(Directory, error);
        }
    } cleanup{file, directory};
    {
        std::ofstream output(file, std::ios::binary);
        output << "abc";
    }
    std::array<uint8_t, 32> digest{};
    const std::array<uint8_t, 32> expected{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                           0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    REQUIRE(FingerprintPluginFile(file, digest));
    REQUIRE(digest == expected);
    {
        std::ofstream output(file, std::ios::binary);
        output << "abcd";
    }
    REQUIRE(FingerprintPluginFile(file, digest));
    REQUIRE(digest != expected);
    REQUIRE_FALSE(FingerprintPluginFile(directory / "missing.esp", digest));
    REQUIRE(std::all_of(digest.begin(), digest.end(), [](uint8_t c) { return c == 0; }));
}
