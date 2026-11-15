#include "ContentCompatibility.h"
#include "AlternateStartPolicy.h"
#include "AnniversaryContent.h"

#include <algorithm>
#include <set>

namespace ContentCompatibility
{
namespace
{
bool ValidPluginName(const String& acName) noexcept
{
    return !acName.empty() && acName.size() <= 255 && acName.find_first_of("/\\:") == String::npos &&
           std::none_of(acName.begin(), acName.end(), [](unsigned char c) { return c < 32 || c == 127; }) &&
           (acName.ends_with(".esm") || acName.ends_with(".esp") || acName.ends_with(".esl"));
}

bool ValidResourceName(const String& acName) noexcept
{
    if (acName.empty() || acName.size() > 255 || acName.front() == '/' || acName.back() == '/' || acName.find_first_of("\\:") != String::npos ||
        std::any_of(acName.begin(), acName.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    size_t start = 0;
    while (start < acName.size())
    {
        const size_t end = acName.find('/', start);
        const auto part = acName.substr(start, end == String::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (end == String::npos)
            break;
        start = end + 1;
    }
    return true;
}
} // namespace

String NormalizeName(const String& acName) noexcept
{
    String name = acName;
    for (char& value : name)
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
    return name;
}

Result Validate(const Mods& acMods) noexcept
{
    if (acMods.ModList.empty() || acMods.ModList.size() > 4096)
        return {Issue::InvalidManifest, {}};
    // Existing actor messages reserve server mod zero for Skyrim.esm/player references.
    const auto& base = acMods.ModList.front();
    if (NormalizeName(base.Filename) != "skyrim.esm" || base.IsLite || base.Id != 0)
        return {Issue::InvalidManifest, {}};
    size_t bytes = 4;
    std::set<String> names;
    std::set<uint32_t> slots;
    for (const auto& entry : acMods.ModList)
    {
        const auto name = NormalizeName(entry.Filename);
        if (!ValidPluginName(name) || entry.Masters.size() > 254)
            return {Issue::InvalidManifest, {}};
        if ((!entry.IsLite && entry.Id >= 0xFE) || (entry.IsLite && entry.Id > 0xFFF) || !names.insert(name).second ||
            !slots.insert(entry.Id | (entry.IsLite ? 0x10000u : 0u)).second)
            return {Issue::InvalidManifest, entry.Filename};
        bytes += name.size() + 32 + 16;
        if (bytes > kMaxManifestBytes)
            return {Issue::InvalidManifest, {}};
        if (std::all_of(entry.Digest.begin(), entry.Digest.end(), [](uint8_t byte) { return byte == 0; }))
            return {Issue::UnreadablePlugin, entry.Filename};
        std::set<String> masters;
        for (const auto& master : entry.Masters)
        {
            const auto normalized = NormalizeName(master);
            if (!ValidPluginName(normalized) || normalized == name || !masters.insert(normalized).second)
                return {Issue::InvalidManifest, entry.Filename};
            bytes += master.size() + 8;
            if (bytes > kMaxManifestBytes)
                return {Issue::InvalidManifest, {}};
        }
    }
    for (auto entry = acMods.ModList.begin(); entry != acMods.ModList.end(); ++entry)
    {
        for (const auto& master : entry->Masters)
        {
            const auto dependency =
                std::find_if(acMods.ModList.begin(), acMods.ModList.end(), [&](const auto& candidate) { return NormalizeName(candidate.Filename) == NormalizeName(master); });
            if (dependency == acMods.ModList.end())
                return {Issue::MissingDependency, entry->Filename + " -> " + master};
            if (dependency >= entry)
                return {Issue::DifferentOrder, entry->Filename + " -> " + master};
        }
    }
    if (acMods.Resources.size() > 4096)
        return {Issue::InvalidManifest, {}};
    std::set<String> resources;
    for (const auto& resource : acMods.Resources)
    {
        const auto name = NormalizeName(resource.Filename);
        if (!ValidResourceName(name) || !resources.insert(name).second)
            return {Issue::InvalidManifest, resource.Filename};
        bytes += name.size() + 40;
        if (bytes > kMaxManifestBytes)
            return {Issue::InvalidManifest, {}};
        if (std::all_of(resource.Digest.begin(), resource.Digest.end(), [](uint8_t byte) { return byte == 0; }))
            return {Issue::UnreadableResource, resource.Filename};
    }
    for (const char* required : {"runtime/address-library", "data/loose-files"})
        if (!resources.contains(required))
            return {Issue::MissingResource, required};
    for (const auto& entry : acMods.ModList)
    {
        const auto name = NormalizeName(entry.Filename);
        if (std::find(kAnniversaryPlugins.begin(), kAnniversaryPlugins.end(), std::string_view(name.data(), name.size())) != kAnniversaryPlugins.end())
        {
            const auto archive = "data/" + name.substr(0, name.size() - 4) + ".bsa";
            if (!resources.contains(archive))
                return {Issue::MissingResource, archive};
        }
    }
    const auto start = std::find_if(acMods.ModList.begin(), acMods.ModList.end(), [](const auto& entry) { return NormalizeName(entry.Filename) == AlternateStartPolicy::cPlugin; });
    if (start != acMods.ModList.end())
    {
        const auto dependency =
            std::find_if(acMods.ModList.begin(), acMods.ModList.end(), [](const auto& entry) { return NormalizeName(entry.Filename) == AlternateStartPolicy::cDependency; });
        if (dependency == acMods.ModList.end())
            return {Issue::MissingDependency, "Unofficial Skyrim Special Edition Patch.esp"};
        if (dependency >= start)
            return {Issue::DifferentOrder, start->Filename};
    }
    return {};
}

Result Compare(const Mods& acExpected, const Mods& acActual) noexcept
{
    if (const auto valid = Validate(acExpected); !valid)
        return valid;
    if (const auto valid = Validate(acActual); !valid)
        return valid;
    for (const auto& expected : acExpected.ModList)
    {
        const auto name = NormalizeName(expected.Filename);
        const auto actual = std::find_if(acActual.ModList.begin(), acActual.ModList.end(), [&](const auto& entry) { return NormalizeName(entry.Filename) == name; });
        if (actual == acActual.ModList.end())
            return {Issue::MissingPlugin, expected.Filename};
        if (actual->IsLite != expected.IsLite || actual->Digest != expected.Digest)
            return {Issue::DifferentPlugin, expected.Filename};
        if (actual->Masters.size() != expected.Masters.size())
            return {Issue::DifferentPlugin, expected.Filename};
        for (size_t i = 0; i < expected.Masters.size(); ++i)
            if (NormalizeName(actual->Masters[i]) != NormalizeName(expected.Masters[i]))
                return {Issue::DifferentPlugin, expected.Filename};
    }
    for (const auto& actual : acActual.ModList)
        if (std::none_of(acExpected.ModList.begin(), acExpected.ModList.end(), [&](const auto& entry) { return NormalizeName(entry.Filename) == NormalizeName(actual.Filename); }))
            return {Issue::ExtraPlugin, actual.Filename};
    for (size_t i = 0; i < acExpected.ModList.size(); ++i)
        if (NormalizeName(acExpected.ModList[i].Filename) != NormalizeName(acActual.ModList[i].Filename))
            return {Issue::DifferentOrder, acActual.ModList[i].Filename};
    for (const auto& expected : acExpected.Resources)
    {
        const auto actual = std::find_if(
            acActual.Resources.begin(), acActual.Resources.end(), [&](const auto& entry) { return NormalizeName(entry.Filename) == NormalizeName(expected.Filename); });
        if (actual == acActual.Resources.end())
            return {Issue::MissingResource, expected.Filename};
        if (actual->Digest != expected.Digest)
            return {Issue::DifferentResource, expected.Filename};
    }
    for (const auto& actual : acActual.Resources)
        if (std::none_of(
                acExpected.Resources.begin(), acExpected.Resources.end(), [&](const auto& entry) { return NormalizeName(entry.Filename) == NormalizeName(actual.Filename); }))
            return {Issue::ExtraResource, actual.Filename};
    return {};
}

String Describe(const Result& acResult) noexcept
{
    const char* pReason = "Compatible active plugins";
    switch (acResult.Reason)
    {
    case Issue::InvalidManifest: pReason = "Invalid or oversized content manifest. Restore a supported profile and keep its manifest below 48 KiB"; break;
    case Issue::UnreadablePlugin: pReason = "Cannot read a valid active plugin. Verify its files and stop modifying them during connection"; break;
    case Issue::MissingPlugin: pReason = "Missing active plugin. Install and enable the same legally owned version as the host"; break;
    case Issue::ExtraPlugin: pReason = "Extra active plugin. Select the same active profile as the host"; break;
    case Issue::DifferentPlugin: pReason = "Plugin bytes, masters or ESL flag differ. Install the same plugin version as the host"; break;
    case Issue::DifferentOrder: pReason = "Plugin load order differs or a master loads too late. Match the host order with masters before dependents"; break;
    case Issue::MissingDependency: pReason = "Missing active master. Install and enable this dependency before its dependent plugin"; break;
    case Issue::UnreadableResource: pReason = "Cannot fingerprint resource. Verify the file and stop modifying it during connection"; break;
    case Issue::MissingResource: pReason = "Missing required resource. Verify the same game, library and mod installation as the host"; break;
    case Issue::ExtraResource: pReason = "Extra resource or native library. Match the host installation/profile"; break;
    case Issue::DifferentResource: pReason = "Resource or library bytes differ. Match the host version; for data/loose-files compare loose assets and scripts"; break;
    case Issue::None: break;
    }
    return String(pReason) + (acResult.Filename.empty() ? String{} : ": " + acResult.Filename);
}

String EscapeJson(const String& acText) noexcept
{
    String result;
    for (unsigned char c : acText)
    {
        if (c == '"' || c == '\\')
            result += '\\';
        if (c >= 32)
            result += static_cast<char>(c);
    }
    return result;
}

bool EncodeInitialForm(uint32_t aRuntimeForm, const Mods& acLocalMods, GameId& aId) noexcept
{
    aId = {};
    if (aRuntimeForm == 0)
        return true;
    const bool cLite = (aRuntimeForm >> 24) == 0xFE;
    const uint32_t slot = cLite ? (aRuntimeForm >> 12) & 0xFFF : aRuntimeForm >> 24;
    for (size_t i = 0; i < acLocalMods.ModList.size(); ++i)
    {
        const auto& mod = acLocalMods.ModList[i];
        if (mod.IsLite == cLite && mod.Id == slot)
        {
            aId.ModId = static_cast<uint32_t>(i + 1);
            aId.BaseId = aRuntimeForm & (cLite ? 0xFFFu : 0xFFFFFFu);
            return true;
        }
    }
    return false;
}

bool ResolveInitialForm(const GameId& acId, const Mods& acLocalMods, const Mods& acServerMods, GameId& aResolved) noexcept
{
    aResolved = {};
    if (acId.ModId == 0)
        return acId.BaseId == 0;
    if (acId.ModId > acLocalMods.ModList.size())
        return false;
    const auto& local = acLocalMods.ModList[acId.ModId - 1];
    if (acId.BaseId > (local.IsLite ? 0xFFFu : 0xFFFFFFu))
        return false;
    for (const auto& server : acServerMods.ModList)
        if (NormalizeName(server.Filename) == NormalizeName(local.Filename) && server.IsLite == local.IsLite)
        {
            aResolved.ModId = server.Id;
            aResolved.BaseId = acId.BaseId;
            return true;
        }
    return false;
}
} // namespace ContentCompatibility
