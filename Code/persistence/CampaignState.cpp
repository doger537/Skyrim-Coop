#include "CampaignState.h"
#include "CorePluginUpgrade.h"
#include <Structs/ContentCompatibility.h>
#include <algorithm>
#include <limits>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

namespace Campaign
{
bool State::Open(const std::filesystem::path& acPath, const String& acAdapters) noexcept
{
    m_ready = false;
    m_contract.reset();
    m_worldEpoch = 0;
    m_error.clear();
    if (!m_store.Open(acPath) || !m_store.VerifyRecords())
    {
        m_error = m_store.Error();
        return false;
    }
    WorldStore::Record record;
    const auto result = m_store.Read("$contract", record);
    if (result == WorldStore::ReadResult::Failed)
    {
        m_error = m_store.Error();
        return false;
    }
    if (result == WorldStore::ReadResult::Found)
    {
        Contract contract;
        if (!Decode(record.Value, contract) || (contract.Runtime != "1.7.104.0" && contract.Runtime != "1.6.1170.0" && contract.Runtime != "1.6.1179.0"))
        {
            m_error = "Saved world content contract is invalid or unsupported; preserve the database";
            return false;
        }
        m_contract = std::move(contract);
    }
    else if (m_store.Revision() != 0)
    {
        m_error = "World has records but its content contract is missing; restore a verified backup";
        return false;
    }
    if (!LoadQuestState(acAdapters))
        return false;
    if (m_contract)
    {
        // A narrowly pinned additive upgrade of our own ESP changes no existing
        // FormIDs/records. Do it before binding the restored server contract.
        // Unknown edits, other mods, resources and downgrades stay incompatible.
        if (m_contract->Runtime == "1.7.104.0" && CorePluginUpgrade::Upgrade(m_contract->Content) && !m_adapters.Validate(m_contract->Content))
        {
            m_error = "The additive core plugin update conflicts with a pinned quest adapter: " + m_adapters.Error();
            return false;
        }
        // Upgrade only after validating the pinned policy and journal. Older H
        // servers reject contract v2 instead of silently ignoring the common story.
        Vector<uint8_t> current;
        if (!Encode(*m_contract, current))
        {
            m_error = "Cannot encode the campaign format upgrade";
            return false;
        }
        if (record.Value != current)
        {
            record.Value = std::move(current);
            uint64_t revision = 0;
            if (m_store.Commit({record}, revision) != WorldStore::WriteResult::Committed)
            {
                m_error = "Cannot commit campaign format upgrade: " + m_store.Error();
                return false;
            }
        }
    }
    WorldStore::Record epoch;
    const auto epochResult = m_store.Read("$world-epoch", epoch);
    if (epochResult == WorldStore::ReadResult::Failed ||
        (epochResult == WorldStore::ReadResult::Found && epoch.Value != Vector<uint8_t>{'T', 'C', 'W', 'E', 1}))
    {
        m_error = "Cannot read the authoritative world timeline marker; preserve the database";
        return false;
    }
    if (epochResult == WorldStore::ReadResult::Found)
        m_worldEpoch = epoch.Revision;
    m_ready = true;
    return true;
}

bool State::AdvanceWorldEpoch() noexcept
{
    if (!m_ready || !m_contract)
    {
        m_error = "Cannot start a timeline before the world content contract is bound";
        return false;
    }
    WorldStore::Record marker;
    marker.Key = "$world-epoch";
    marker.Revision = m_worldEpoch;
    marker.Value = {'T', 'C', 'W', 'E', 1};
    uint64_t revision{};
    if (m_store.Commit({marker}, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = "Cannot persist a new world timeline: " + m_store.Error();
        return false;
    }
    m_worldEpoch = revision;
    return true;
}

bool State::RegisterParticipant(const CharacterIdentity& acIdentity) noexcept
{
    if (!m_ready || !m_contract || !acIdentity.IsValid())
    {
        m_error = "Cannot register an invalid character or an unbound world";
        return false;
    }
    WorldStore::Record record;
    record.Key = "$participant/";
    constexpr char hex[] = "0123456789abcdef";
    for (uint16_t part : acIdentity.Parts)
        for (int shift = 12; shift >= 0; shift -= 4)
            record.Key += hex[(part >> shift) & 15];
    const auto result = m_store.Read(record.Key, record);
    const Vector<uint8_t> format{'T', 'C', 'P', 'I', 1};
    if (result == WorldStore::ReadResult::Found && record.Value == format)
        return true;
    if (result != WorldStore::ReadResult::Missing)
    {
        m_error = "Cannot read the durable character identity record; preserve the database";
        return false;
    }
    record.Value = format;
    uint64_t revision{};
    if (m_store.Commit({record}, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = "Cannot persist the character identity: " + m_store.Error();
        return false;
    }
    return true;
}

bool State::Bind(const String& acRuntime, const Mods& acContent) noexcept
{
    if (!m_ready)
        return false;
    if (m_contract)
    {
        if (m_contract->Runtime != acRuntime)
        {
            m_error = "Saved world requires Skyrim runtime " + m_contract->Runtime;
            return false;
        }
        const auto compatible = ContentCompatibility::Compare(m_contract->Content, acContent);
        if (!compatible)
            m_error = ContentCompatibility::Describe(compatible);
        return static_cast<bool>(compatible);
    }
    Contract contract{acRuntime, acContent};
    if (!m_adapters.Validate(acContent))
    {
        m_error = m_adapters.Error();
        return false;
    }
    WorldStore::Record record;
    record.Key = "$contract";
    if ((acRuntime != "1.7.104.0" && acRuntime != "1.6.1170.0" && acRuntime != "1.6.1179.0") || !Encode(contract, record.Value))
    {
        m_error = "Cannot persist an invalid content contract";
        return false;
    }
    uint64_t revision = 0;
    WorldStore::Record adapters;
    adapters.Key = "$adapters";
    const auto& canonical = m_adapters.Canonical();
    adapters.Value.assign(canonical.begin(), canonical.end());
    if (m_store.Commit({record, adapters}, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return false;
    }
    m_contract = std::move(contract);
    return true;
}

bool State::ValidForm(GameId aId, bool aOptional) const noexcept
{
    if (aId == GameId{})
        return aOptional;
    return m_contract && aId.ModId < m_contract->Content.ModList.size() && aId.BaseId != 0 && aId.BaseId <= (m_contract->Content.ModList[aId.ModId].IsLite ? 0xFFFu : 0xFFFFFFu);
}

String State::Key(const char* apKind, GameId aId) const noexcept
{
    if (!ValidForm(aId))
        return {};
    return String(apKind) + "/" + ContentCompatibility::NormalizeName(m_contract->Content.ModList[aId.ModId].Filename) + "/" + std::to_string(aId.BaseId).c_str();
}

bool State::ValidObject(const ObjectData& acObject) const noexcept
{
    if (!ValidForm(acObject.Id) || !ValidForm(acObject.CellId) || !ValidForm(acObject.WorldSpaceId, true) || acObject.CurrentInventory.Entries.size() > 4096)
        return false;
    for (const auto& entry : acObject.CurrentInventory.Entries)
    {
        if (!ValidForm(entry.BaseId) || entry.Count <= 0 || !ValidForm(entry.ExtraPoisonId, true) || entry.EnchantData.Effects.size() > 256)
            return false;
        // Existing client code reconstructs dynamic enchantments from these effects.
        if (entry.ExtraEnchantId.ModId == UINT32_MAX)
        {
            if (entry.EnchantData.Effects.empty())
                return false;
        }
        else if (!ValidForm(entry.ExtraEnchantId, true))
            return false;
        for (const auto& effect : entry.EnchantData.Effects)
            if (!ValidForm(effect.EffectId))
                return false;
    }
    const auto& magic = acObject.CurrentInventory.CurrentMagicEquipment;
    return ValidForm(magic.LeftHandSpell, true) && ValidForm(magic.RightHandSpell, true) && ValidForm(magic.Shout, true);
}

bool State::LoadObject(GameId aId, ObjectData& aObject, bool& aFound) noexcept
{
    aFound = false;
    const auto key = Key("object", aId);
    if (!m_ready || key.empty())
    {
        m_error = "Object has no stable plugin/form identity in this world";
        return false;
    }
    WorldStore::Record record;
    const auto read = m_store.Read(key, record);
    if (read == WorldStore::ReadResult::Failed)
    {
        m_error = m_store.Error();
        return false;
    }
    if (read == WorldStore::ReadResult::Missing)
        return true;
    if (!Decode(record.Value, aObject) || aObject.Id != aId || !ValidObject(aObject))
    {
        m_error = "Saved object is invalid or contains unsupported form IDs: " + key;
        return false;
    }
    aFound = true;
    return true;
}

bool State::SaveObject(const ObjectData& acObject) noexcept
{
    return SaveObjects({acObject});
}

bool State::SaveObjects(const Vector<ObjectData>& acObjects) noexcept
{
    Vector<WorldStore::Record> records;
    for (const auto& object : acObjects)
    {
        if (!m_ready || !ValidObject(object))
        {
            m_error = "Cannot save an object with invalid quantities or unmapped/dynamic forms";
            return false;
        }
        WorldStore::Record record;
        record.Key = Key("object", object.Id);
        WorldStore::Record current;
        const auto read = m_store.Read(record.Key, current);
        if (read == WorldStore::ReadResult::Failed)
        {
            m_error = m_store.Error();
            return false;
        }
        record.Revision = read == WorldStore::ReadResult::Found ? current.Revision : 0;
        if (!Encode(object, record.Value))
        {
            m_error = "Object snapshot is non-finite, oversized or invalid";
            return false;
        }
        records.push_back(std::move(record));
    }
    uint64_t revision = 0;
    if (m_store.Commit(records, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return false;
    }
    return true;
}

bool ApplyInventoryDelta(Inventory& aInventory, const Inventory::Entry& acDelta) noexcept
{
    if (acDelta.Count == 0 || acDelta.Count == std::numeric_limits<int32_t>::min())
        return false;
    auto entry = std::find_if(aInventory.Entries.begin(), aInventory.Entries.end(), [&](const auto& candidate) { return candidate.CanBeMerged(acDelta); });
    const int64_t count = int64_t(entry == aInventory.Entries.end() ? 0 : entry->Count) + acDelta.Count;
    if (count < 0 || count > std::numeric_limits<int32_t>::max())
        return false;
    if (entry == aInventory.Entries.end())
    {
        if (count == 0)
            return false;
        aInventory.Entries.push_back(acDelta);
    }
    else if (count == 0)
        aInventory.Entries.erase(entry);
    else
        entry->Count = static_cast<int32_t>(count);
    return true;
}
} // namespace Campaign
