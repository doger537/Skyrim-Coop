#include "ContentCompatibility.h"
#include "AlternateStartPolicy.h"

#include <algorithm>
#include <set>

namespace ContentCompatibility
{
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
    size_t bytes = 2;
    std::set<String> names;
    std::set<uint32_t> slots;
    for (const auto& entry : acMods.ModList)
    {
        const auto name = NormalizeName(entry.Filename);
        if (name.empty() || name.size() > 255 || name.find_first_of("/\\:") != String::npos ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
            !(name.ends_with(".esm") || name.ends_with(".esp") || name.ends_with(".esl")))
            return {Issue::InvalidManifest, {}};
        if ((!entry.IsLite && entry.Id >= 0xFE) || (entry.IsLite && entry.Id > 0xFFF) || !names.insert(name).second ||
            !slots.insert(entry.Id | (entry.IsLite ? 0x10000u : 0u)).second)
            return {Issue::InvalidManifest, entry.Filename};
        bytes += name.size() + 32 + 16;
        if (bytes > kMaxManifestBytes)
            return {Issue::InvalidManifest, {}};
        if (std::all_of(entry.Digest.begin(), entry.Digest.end(), [](uint8_t byte) { return byte == 0; }))
            return {Issue::UnreadablePlugin, entry.Filename};
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
    }
    for (const auto& actual : acActual.ModList)
        if (std::none_of(acExpected.ModList.begin(), acExpected.ModList.end(), [&](const auto& entry) { return NormalizeName(entry.Filename) == NormalizeName(actual.Filename); }))
            return {Issue::ExtraPlugin, actual.Filename};
    for (size_t i = 0; i < acExpected.ModList.size(); ++i)
        if (NormalizeName(acExpected.ModList[i].Filename) != NormalizeName(acActual.ModList[i].Filename))
            return {Issue::DifferentOrder, acActual.ModList[i].Filename};
    return {};
}

String Describe(const Result& acResult) noexcept
{
    const char* pReason = "Compatible active plugins";
    switch (acResult.Reason)
    {
    case Issue::InvalidManifest: pReason = "Invalid or oversized active plugin manifest"; break;
    case Issue::UnreadablePlugin: pReason = "Cannot fingerprint active plugin"; break;
    case Issue::MissingPlugin: pReason = "Missing active plugin required by this session"; break;
    case Issue::ExtraPlugin: pReason = "Extra active plugin is not supported by this session"; break;
    case Issue::DifferentPlugin: pReason = "Active plugin has different contents or ESL flag"; break;
    case Issue::DifferentOrder: pReason = "Active plugin load order differs"; break;
    case Issue::MissingDependency: pReason = "Alternate Start requires this active dependency"; break;
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
