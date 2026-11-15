#include "QuestAdapters.h"
#include <Structs/ContentCompatibility.h>
#include <Structs/AlternateStartPolicy.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <set>

using TiltedPhoques::String;
using TiltedPhoques::Vector;
using Json = nlohmann::json;

namespace Campaign
{
namespace
{
bool UInt(const Json& acValue, uint32_t& aValue, uint32_t aMax) noexcept
{
    if (!acValue.is_number_integer())
        return false;
    if (acValue.is_number_unsigned())
    {
        const uint64_t value = acValue.get<uint64_t>();
        if (value > aMax)
            return false;
        aValue = static_cast<uint32_t>(value);
        return true;
    }
    const int64_t value = acValue.get<int64_t>();
    if (value < 0 || static_cast<uint64_t>(value) > aMax)
        return false;
    aValue = static_cast<uint32_t>(value);
    return true;
}

bool Text(const Json& acValue, String& aText, size_t aLimit) noexcept
{
    if (!acValue.is_string())
        return false;
    const auto& value = acValue.get_ref<const std::string&>();
    if (value.empty() || value.size() > aLimit || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    aText.assign(value.data(), value.size());
    return true;
}

bool Stages(const Json& acValue, Vector<uint16_t>& aStages, size_t aMin = 0) noexcept
{
    if (!acValue.is_array() || acValue.size() < aMin || acValue.size() > 4096)
        return false;
    std::set<uint16_t> unique;
    for (const auto& value : acValue)
    {
        uint32_t stage = 0;
        if (!UInt(value, stage, 65535) || !unique.insert(static_cast<uint16_t>(stage)).second)
            return false;
        aStages.push_back(static_cast<uint16_t>(stage));
    }
    return true;
}

bool Flag(const Json& acObject, const char* apKey, bool& aFlag) noexcept
{
    if (!acObject.contains(apKey))
        return true;
    if (!acObject[apKey].is_boolean())
        return false;
    aFlag = acObject[apKey].get<bool>();
    return true;
}

bool BoundedJson(std::string_view acText) noexcept
{
    if (acText.size() > 256 * 1024)
        return false;
    unsigned depth = 0;
    bool string = false;
    bool escape = false;
    for (const char value : acText)
    {
        if (string)
        {
            if (escape)
                escape = false;
            else if (value == '\\')
                escape = true;
            else if (value == '"')
                string = false;
        }
        else if (value == '"')
            string = true;
        else if (value == '{' || value == '[')
        {
            if (++depth > 32)
                return false;
        }
        else if (value == '}' || value == ']')
        {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !string && depth == 0;
}
} // namespace

bool QuestAdapters::Load(std::string_view acJson) noexcept
{
    m_adapters.clear();
    m_canonical.clear();
    m_error = "Invalid quest adapter file: require schema 1, verified evidence, exact plugin SHA-256 and explicit stage/replay/decision rules";
    if (!BoundedJson(acJson))
        return false;
    const auto json = Json::parse(acJson, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("schema") || json["schema"] != 1 || !json.contains("adapters") || !json["adapters"].is_array() ||
        json["adapters"].size() > 256)
        return false;
    std::set<String> unique;
    for (const auto& entry : json["adapters"])
    {
        QuestAdapter adapter;
        for (const char* key : {"plugin", "sha256", "quest", "stages", "replay_stages", "evidence", "game_verified"})
            if (!entry.is_object() || !entry.contains(key))
                return false;
        String digest;
        String form;
        if (!Text(entry["plugin"], adapter.Plugin, 255) || !Text(entry["sha256"], digest, 64) || digest.size() != 64 || !Text(entry["quest"], form, 8) ||
            !Text(entry["evidence"], adapter.Evidence, 1024) || !entry["game_verified"].is_boolean() || !entry["game_verified"].get<bool>() ||
            !Flag(entry, "allow_misc", adapter.AllowMisc) || !Flag(entry, "allow_restart", adapter.AllowRestart) || !Flag(entry, "replay_lifecycle", adapter.ReplayLifecycle) ||
            !Stages(entry["stages"], adapter.Stages, 1) || !Stages(entry["replay_stages"], adapter.ReplayStages))
            return false;
        adapter.Plugin = ContentCompatibility::NormalizeName(adapter.Plugin);
        if (adapter.Plugin.find_first_of("/\\:") != String::npos || adapter.Plugin == AlternateStartPolicy::cPlugin ||
            !(adapter.Plugin.ends_with(".esm") || adapter.Plugin.ends_with(".esp") || adapter.Plugin.ends_with(".esl")))
            return false;
        const auto parsed = std::from_chars(form.data(), form.data() + form.size(), adapter.Form, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != form.data() + form.size() || adapter.Form == 0 || adapter.Form > 0xFFFFFF)
            return false;
        for (size_t i = 0; i < adapter.Digest.size(); ++i)
        {
            uint32_t byte = 0;
            const auto hex = std::from_chars(digest.data() + i * 2, digest.data() + i * 2 + 2, byte, 16);
            if (hex.ec != std::errc{} || hex.ptr != digest.data() + i * 2 + 2)
                return false;
            adapter.Digest[i] = static_cast<uint8_t>(byte);
        }
        if (std::all_of(adapter.Digest.begin(), adapter.Digest.end(), [](uint8_t byte) { return byte == 0; }))
            return false;
        for (uint16_t stage : adapter.ReplayStages)
            if (std::find(adapter.Stages.begin(), adapter.Stages.end(), stage) == adapter.Stages.end())
                return false;
        if (entry.contains("decisions"))
        {
            const auto& decisions = entry["decisions"];
            if (!decisions.is_array() || decisions.size() > 256)
                return false;
            std::set<String> groups;
            for (const auto& decision : decisions)
            {
                Branch branch;
                if (!decision.is_object() || !decision.contains("id") || !decision.contains("stages") || !Text(decision["id"], branch.Id, 64) ||
                    !Stages(decision["stages"], branch.Stages, 2) || !groups.insert(branch.Id).second)
                    return false;
                for (uint16_t stage : branch.Stages)
                    if (std::find(adapter.Stages.begin(), adapter.Stages.end(), stage) == adapter.Stages.end())
                        return false;
                adapter.Branches.push_back(std::move(branch));
            }
        }
        const auto key = adapter.Plugin + "/" + std::to_string(adapter.Form).c_str();
        if (!unique.insert(key).second)
            return false;
        m_adapters.push_back(std::move(adapter));
    }
    m_canonical = json.dump().c_str();
    m_error.clear();
    return true;
}

bool QuestAdapters::Validate(const Mods& acContent) noexcept
{
    for (const auto& adapter : m_adapters)
    {
        const auto mod = std::find_if(
            acContent.ModList.begin(), acContent.ModList.end(), [&](const auto& entry) { return ContentCompatibility::NormalizeName(entry.Filename) == adapter.Plugin; });
        if (mod == acContent.ModList.end() || mod->Digest != adapter.Digest || adapter.Form > (mod->IsLite ? 0xFFFu : 0xFFFFFFu))
        {
            m_error = "Quest adapter does not match the pinned plugin bytes or ESL form range: " + adapter.Plugin;
            return false;
        }
    }
    return true;
}

const QuestAdapter* QuestAdapters::Find(const String& acPlugin, uint32_t aForm) const noexcept
{
    const auto plugin = ContentCompatibility::NormalizeName(acPlugin);
    const auto adapter = std::find_if(m_adapters.begin(), m_adapters.end(), [&](const auto& entry) { return entry.Plugin == plugin && entry.Form == aForm; });
    return adapter == m_adapters.end() ? nullptr : &*adapter;
}
} // namespace Campaign
