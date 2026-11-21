#include "CampaignState.h"
#include <algorithm>
#include <limits>

using TiltedPhoques::String;
using TiltedPhoques::Vector;
namespace Campaign
{
namespace
{
String Hex(const CharacterIdentity& acIdentity) noexcept
{
    constexpr char hex[] = "0123456789abcdef";
    String value;
    for (uint16_t part : acIdentity.Parts)
        for (int shift = 12; shift >= 0; shift -= 4) value += hex[(part >> shift) & 15];
    return value;
}
String Inbox(const CharacterIdentity& acIdentity) noexcept { return "item-inbox/" + Hex(acIdentity) + "/"; }
String Operation(const CharacterIdentity& acIdentity, const CharacterIdentity& acSession, uint64_t aOperation) noexcept
{ return "item-operation/" + Hex(acIdentity) + "/" + Hex(acSession) + "/" + std::to_string(aOperation).c_str(); }
String ReceiptKey(uint64_t aRevision) noexcept { return String("item-receipt/") + std::to_string(aRevision).c_str(); }
String ItemKey(uint64_t aId) noexcept { return String("world-item/") + std::to_string(aId).c_str(); }
bool Reference(const Vector<uint8_t>& acBytes, String& aKey) noexcept
{
    if (acBytes.empty() || acBytes.size() > 64 || std::find(acBytes.begin(), acBytes.end(), 0) != acBytes.end()) return false;
    aKey.assign(reinterpret_cast<const char*>(acBytes.data()), acBytes.size());
    return true;
}
}
bool State::ReadValidItemReceipt(const WorldStore::Record& acRecord, WorldItemReceipt& aReceipt) noexcept
{
    WorldStore::Record participant, itemRecord;
    WorldItemState item;
    if (!Decode(acRecord.Value, aReceipt) || acRecord.Key != ReceiptKey(acRecord.Revision) ||
        aReceipt.State.Revision != acRecord.Revision || !ValidWorldItem(aReceipt.State) ||
        m_store.Read("$participant/" + Hex(aReceipt.Recipient), participant) != WorldStore::ReadResult::Found ||
        participant.Value != Vector<uint8_t>{'T', 'C', 'P', 'I', 1} ||
        m_store.Read(ItemKey(aReceipt.State.Id), itemRecord) != WorldStore::ReadResult::Found ||
        !Decode(itemRecord.Value, item) || !ValidWorldItem(item) || item.Id != aReceipt.State.Id ||
        item.Revision != itemRecord.Revision || itemRecord.Revision < acRecord.Revision ||
        item.Item.BaseId != aReceipt.State.Item.BaseId || !item.Item.IsExtraDataEquals(aReceipt.State.Item))
    { m_error = "Saved item receipt lacks its valid lifecycle, recipient or revision"; return false; }
    return true;
}
bool State::ValidateItemReceiptIndexes() noexcept
{
    Vector<WorldStore::Record> records;
    uint64_t cursor{}, count{};
    do
    {
        if (!m_store.ReadPrefixAfterRevision("item-receipt/", cursor, 64, records)) { m_error = m_store.Error(); return false; }
        for (const auto& record : records)
        {
            WorldItemReceipt receipt;
            if (!ReadValidItemReceipt(record, receipt)) return false;
            const Vector<uint8_t> reference(record.Key.begin(), record.Key.end());
            for (const auto& key : {Inbox(receipt.Recipient) + record.Key, Operation(receipt.Recipient, receipt.Session, receipt.Operation)})
            {
                WorldStore::Record index;
                if (m_store.Read(key, index) != WorldStore::ReadResult::Found || index.Revision != record.Revision || index.Value != reference)
                { m_error = "World item receipt has an incomplete inbox/operation index"; return false; }
            }
            cursor = record.Revision; ++count;
        }
    } while (!records.empty());
    for (const String prefix : {String("item-inbox/"), String("item-operation/")})
    {
        uint64_t indexes{}; cursor = 0;
        do
        {
            if (!m_store.ReadPrefixAfterRevision(prefix, cursor, 64, records)) { m_error = m_store.Error(); return false; }
            indexes += records.size();
            if (!records.empty()) cursor = records.back().Revision;
        } while (!records.empty());
        if (indexes != count) { m_error = "World item receipt has orphan inbox/operation indexes"; return false; }
    }
    return true;
}
State::ItemTransferResult State::TransferWorldItem(WorldItemState& aItem, uint64_t aExpectedRevision,
    const CharacterIdentity& acRecipient, const CharacterIdentity& acSession, uint64_t aOperation,
    int32_t aDelta, WorldItemReceipt& aReceipt) noexcept
{
    aReceipt = {};
    if (!m_ready || !m_contract || !m_worldEpoch || !acRecipient.IsValid() || !acSession.IsValid() || !aOperation ||
        !aDelta || aDelta < -32767 || aDelta > 32767 || m_store.Revision() >= uint64_t(std::numeric_limits<int64_t>::max()))
    { m_error = "Invalid or unbound world-item transfer"; return ItemTransferResult::Invalid; }
    const String operation = Operation(acRecipient, acSession, aOperation);
    WorldStore::Record existing;
    const auto read = m_store.Read(operation, existing);
    if (read == WorldStore::ReadResult::Failed) { m_error = m_store.Error(); return ItemTransferResult::StorageError; }
    if (read == WorldStore::ReadResult::Found)
    {
        String key;
        WorldStore::Record record, inbox;
        if (!Reference(existing.Value, key) || m_store.Read(key, record) != WorldStore::ReadResult::Found ||
            existing.Revision != record.Revision || !ReadValidItemReceipt(record, aReceipt) ||
            m_store.Read(Inbox(acRecipient) + key, inbox) != WorldStore::ReadResult::Found || inbox.Revision != record.Revision || inbox.Value != existing.Value)
        { m_error = "Cannot replay a valid committed item transfer"; return ItemTransferResult::StorageError; }
        if (aReceipt.Recipient != acRecipient || aReceipt.Session != acSession || aReceipt.Operation != aOperation ||
            aReceipt.Delta != aDelta || aReceipt.ExpectedRevision != aExpectedRevision || (aItem.Id && aItem.Id != aReceipt.State.Id) ||
            (aDelta < 0 && (aItem.Item != aReceipt.State.Item || aExpectedRevision)) || (aDelta > 0 && !aItem.Id))
        { m_error = "An item operation was reused with different effects"; return ItemTransferResult::Conflict; }
        aItem = aReceipt.State;
        return ItemTransferResult::Duplicate;
    }
    WorldStore::Record participant;
    if (m_store.Read("$participant/" + Hex(acRecipient), participant) != WorldStore::ReadResult::Found ||
        participant.Value != Vector<uint8_t>{'T', 'C', 'P', 'I', 1})
    { m_error = "World-item transfer recipient is not a durable participant"; return ItemTransferResult::Invalid; }
    WorldItemState candidate = aItem;
    WorldStore::Record itemRecord;
    if (aDelta < 0)
    {
        if (candidate.Id || aExpectedRevision || candidate.Deleted || candidate.Item.Count != -aDelta)
            return ItemTransferResult::Invalid;
        candidate.Id = m_store.Revision() + 1;
        itemRecord.Key = ItemKey(candidate.Id);
    }
    else
    {
        itemRecord.Key = ItemKey(candidate.Id);
        const auto result = m_store.Read(itemRecord.Key, itemRecord);
        if (result == WorldStore::ReadResult::Failed) { m_error = m_store.Error(); return ItemTransferResult::StorageError; }
        if (result == WorldStore::ReadResult::Missing || itemRecord.Revision != aExpectedRevision) return ItemTransferResult::Conflict;
        WorldItemState current;
        if (!Decode(itemRecord.Value, current) || !ValidWorldItem(current) || current.Id != candidate.Id || current.Revision != itemRecord.Revision)
        { m_error = "Cannot claim an unreadable dropped-object record"; return ItemTransferResult::StorageError; }
        if (current.Deleted || current.Item.Count < aDelta) return ItemTransferResult::Conflict;
        if (candidate.Item != current.Item || candidate.Deleted || candidate.Revision != current.Revision ||
            (current.WorldEpoch == m_worldEpoch && (candidate.HostEpoch < current.HostEpoch || candidate.Sequence < current.Sequence || candidate.Tick < current.Tick)) ||
            candidate.Sequence == UINT64_MAX || current.Sequence == UINT64_MAX) return ItemTransferResult::Invalid;
        candidate.Sequence = std::max(current.Sequence, candidate.Sequence) + 1;
        candidate.Deleted = aDelta == current.Item.Count;
        // Tombstones keep the original positive stack payload. No zero-count
        // invalid state can accidentally resurrect a fully claimed object.
        if (!candidate.Deleted) candidate.Item.Count -= aDelta;
    }
    candidate.Revision = m_store.Revision() + 1;
    WorldItemReceipt receipt{candidate, acRecipient, acSession, aOperation, aDelta, aExpectedRevision};
    WorldStore::Record record; record.Key = ReceiptKey(candidate.Revision);
    if (candidate.WorldEpoch != m_worldEpoch || !ValidWorldItem(candidate) || !receipt.IsValid() ||
        !Encode(candidate, itemRecord.Value) || !Encode(receipt, record.Value))
    { m_error = "Cannot commit an invalid item lifecycle/inventory effect"; return ItemTransferResult::Invalid; }
    const Vector<uint8_t> reference(record.Key.begin(), record.Key.end());
    uint64_t revision{};
    const auto result = m_store.Commit({itemRecord, record, {Inbox(acRecipient) + record.Key, reference, 0}, {operation, reference, 0}}, revision);
    if (result != WorldStore::WriteResult::Committed)
    { m_error = m_store.Error(); return result == WorldStore::WriteResult::Conflict ? ItemTransferResult::Conflict : ItemTransferResult::StorageError; }
    aItem = std::move(candidate); aReceipt = std::move(receipt);
    return ItemTransferResult::Committed;
}
bool State::ReadWorldItemReceipts(const CharacterIdentity& acRecipient, uint64_t aAfterRevision, Vector<WorldItemReceipt>& aReceipts) noexcept
{
    aReceipts.clear();
    if (!m_ready || !m_contract || !acRecipient.IsValid() || aAfterRevision > m_store.Revision())
    { m_error = "Item receipt cursor is invalid or newer than the authoritative world"; return false; }
    const auto prefix = Inbox(acRecipient);
    Vector<WorldStore::Record> indexes;
    if (!m_store.ReadPrefixAfterRevision(prefix, aAfterRevision, 64, indexes)) { m_error = m_store.Error(); return false; }
    for (const auto& index : indexes)
    {
        String key;
        WorldStore::Record record, operation;
        WorldItemReceipt receipt;
        if (!Reference(index.Value, key) || index.Key != prefix + key || m_store.Read(key, record) != WorldStore::ReadResult::Found ||
            record.Revision != index.Revision || !ReadValidItemReceipt(record, receipt) || receipt.Recipient != acRecipient ||
            m_store.Read(Operation(acRecipient, receipt.Session, receipt.Operation), operation) != WorldStore::ReadResult::Found ||
            operation.Revision != record.Revision || operation.Value != index.Value)
        { m_error = "Cannot read a valid item inventory outbox; preserve the world database"; aReceipts.clear(); return false; }
        aReceipts.push_back(std::move(receipt));
    }
    return true;
}
}
