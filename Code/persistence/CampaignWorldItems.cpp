#include "CampaignState.h"
#include <limits>

namespace Campaign
{
namespace
{
TiltedPhoques::String ItemKey(uint64_t aId) noexcept
{
    return TiltedPhoques::String("world-item/") + std::to_string(aId).c_str();
}
}

bool State::ValidWorldItem(const WorldItemState& acItem) const noexcept
{
    if (!acItem.IsValid())
        return false;
    ObjectData forms;
    forms.Id = acItem.Item.BaseId;
    forms.CellId = acItem.CellId;
    forms.WorldSpaceId = acItem.WorldSpaceId;
    forms.CurrentInventory.Entries.push_back(acItem.Item);
    return ValidObject(forms);
}

bool State::LoadWorldItems(TiltedPhoques::Vector<WorldItemState>& aItems) noexcept
{
    if (!m_ready || !m_contract)
    {
        m_error = "Cannot read dropped objects before the world content contract is bound";
        return false;
    }
    TiltedPhoques::Vector<WorldStore::Record> records;
    if (!m_store.ReadPrefix("world-item/", records))
    {
        m_error = m_store.Error();
        return false;
    }
    TiltedPhoques::Vector<WorldItemState> items;
    items.reserve(records.size());
    for (const auto& record : records)
    {
        WorldItemState item;
        if (!Decode(record.Value, item) || !ValidWorldItem(item) || record.Key != ItemKey(item.Id) || item.Revision != record.Revision || item.Id > item.Revision)
        {
            m_error = "Saved dropped-object identity, revision or content is invalid: " + record.Key;
            return false;
        }
        items.push_back(std::move(item));
    }
    aItems = std::move(items);
    return true;
}

State::ItemWriteResult State::SaveWorldItem(WorldItemState& aItem, uint64_t aExpectedRevision) noexcept
{
    if (!m_ready || !m_contract || m_worldEpoch == 0 || aItem.WorldEpoch != m_worldEpoch ||
        m_store.Revision() >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        m_error = "Dropped object does not belong to the current authoritative world timeline";
        return ItemWriteResult::Invalid;
    }
    WorldItemState candidate = aItem;
    WorldStore::Record record;
    if (candidate.Id == 0)
    {
        if (aExpectedRevision != 0 || candidate.Deleted)
            return ItemWriteResult::Invalid;
        // Store revisions never roll back with Skyrim saves, so this identity
        // cannot be reused by a later spawn, including after a server restart.
        candidate.Id = m_store.Revision() + 1;
        record.Key = ItemKey(candidate.Id);
    }
    else
    {
        record.Key = ItemKey(candidate.Id);
        const auto read = m_store.Read(record.Key, record);
        if (read == WorldStore::ReadResult::Failed)
        {
            m_error = m_store.Error();
            return ItemWriteResult::StorageError;
        }
        if (read == WorldStore::ReadResult::Missing || record.Revision != aExpectedRevision)
            return ItemWriteResult::Conflict;
        WorldItemState current;
        if (!Decode(record.Value, current) || current.Id != candidate.Id || current.Revision != record.Revision)
        {
            m_error = "Cannot update an unreadable dropped-object record";
            return ItemWriteResult::StorageError;
        }
        // Ordinary replication cannot resurrect an acknowledged pickup. Save
        // rollback must use a separate checkpoint/epoch transaction.
        if (current.Deleted || (current.WorldEpoch == candidate.WorldEpoch &&
            (candidate.HostEpoch < current.HostEpoch || (candidate.HostEpoch == current.HostEpoch &&
             (candidate.Sequence <= current.Sequence || candidate.Tick < current.Tick)))))
            return ItemWriteResult::Conflict;
    }
    candidate.Revision = m_store.Revision() + 1;
    if (!ValidWorldItem(candidate) || !Encode(candidate, record.Value))
    {
        m_error = "Cannot persist an invalid or unmapped dropped object";
        return ItemWriteResult::Invalid;
    }
    uint64_t revision{};
    const auto result = m_store.Commit({record}, revision);
    if (result != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return result == WorldStore::WriteResult::Conflict ? ItemWriteResult::Conflict : ItemWriteResult::StorageError;
    }
    aItem = std::move(candidate);
    return ItemWriteResult::Saved;
}
} // namespace Campaign
