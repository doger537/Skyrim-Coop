#include "CampaignState.h"
#include <algorithm>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

namespace Campaign
{
namespace
{
String Inbox(const CharacterIdentity& acCharacter) noexcept
{
    constexpr char hex[] = "0123456789abcdef";
    String prefix("quest-inbox/");
    for (uint16_t part : acCharacter.Parts)
        for (int shift = 12; shift >= 0; shift -= 4)
            prefix += hex[(part >> shift) & 15];
    return prefix + "/";
}
}

bool State::ReadValidQuestGrant(const WorldStore::Record& acRecord, QuestGrantReceipt& aReceipt) noexcept
{
    bool found{};
    QuestRunParticipants participants;
    if (!Decode(acRecord.Value, aReceipt) || !ValidForm(aReceipt.Plan.Quest) ||
        acRecord.Key != Key("quest-grant", aReceipt.Plan.Quest) + "/" + std::to_string(aReceipt.Plan.Run).c_str() + "/" + aReceipt.Plan.Event ||
        !ValidInventory(aReceipt.For(aReceipt.Participants.Players[0])) || !ValidInventory(aReceipt.For(aReceipt.Participants.Players[1])) ||
        !ReadQuestRunParticipants(aReceipt.Plan.Quest, aReceipt.Plan.Run, participants, found) || !found || participants != aReceipt.Participants)
    {
        m_error = "Saved quest grant does not match its content, event or durable participants";
        return false;
    }
    aReceipt.Revision = acRecord.Revision;
    return true;
}

bool State::ValidateQuestGrantIndexes() noexcept
{
    Vector<WorldStore::Record> records;
    uint64_t cursor{}, count{};
    do
    {
        if (!m_store.ReadPrefixAfterRevision("quest-grant/", cursor, 64, records))
        {
            m_error = m_store.Error(); return false;
        }
        for (const auto& record : records)
        {
            QuestGrantReceipt receipt;
            if (!ReadValidQuestGrant(record, receipt))
                return false;
            const Vector<uint8_t> reference(record.Key.begin(), record.Key.end());
            for (const auto& player : receipt.Participants.Players)
            {
                WorldStore::Record index;
                if (m_store.Read(Inbox(player) + record.Key, index) != WorldStore::ReadResult::Found ||
                    index.Revision != record.Revision || index.Value != reference)
                {
                    m_error = "Quest grant recipient index is incomplete";
                    return false;
                }
            }
            cursor = record.Revision; ++count;
        }
    } while (!records.empty());
    uint64_t indexes{}; cursor = 0;
    do
    {
        if (!m_store.ReadPrefixAfterRevision("quest-inbox/", cursor, 64, records))
        {
            m_error = m_store.Error(); return false;
        }
        indexes += records.size();
        if (!records.empty()) cursor = records.back().Revision;
    } while (!records.empty());
    if (indexes != count * 2)
    {
        m_error = "Quest grant outbox has orphan recipient indexes";
        return false;
    }
    return true;
}

State::GrantResult State::CommitQuestGrant(const QuestGrantPlan& acPlan, QuestGrantReceipt& aReceipt) noexcept
{
    aReceipt = {};
    if (!m_ready || !m_bootstrapped || !acPlan.IsValid() || !ValidForm(acPlan.Quest))
    {
        m_error = "Invalid or unbound classified quest grant plan";
        return GrantResult::Invalid;
    }
    Inventory inventory;
    for (const auto& value : acPlan.Items)
        inventory.Entries.push_back(value.Item);
    if (!ValidInventory(inventory))
    {
        m_error = "Quest grant has unmapped item/enchantment forms";
        return GrantResult::Invalid;
    }
    const auto key = Key("quest-grant", acPlan.Quest) + "/" + std::to_string(acPlan.Run).c_str() + "/" + acPlan.Event;
    const Vector<uint8_t> reference(key.begin(), key.end());
    WorldStore::Record existing;
    const auto read = m_store.Read(key, existing);
    if (read == WorldStore::ReadResult::Failed)
    {
        m_error = m_store.Error();
        return GrantResult::StorageError;
    }
    if (read == WorldStore::ReadResult::Found)
    {
        if (!ReadValidQuestGrant(existing, aReceipt))
        {
            m_error = "Saved quest grant receipt is invalid";
            return GrantResult::StorageError;
        }
        if (aReceipt.Plan != acPlan)
        {
            m_error = "Quest grant event was already committed with different effects";
            return GrantResult::Conflict;
        }
        for (const auto& recipient : aReceipt.Participants.Players)
        {
            WorldStore::Record inbox;
            if (m_store.Read(Inbox(recipient) + key, inbox) != WorldStore::ReadResult::Found || inbox.Value != reference || inbox.Revision != existing.Revision)
            {
                m_error = "Quest grant delivery index is incomplete; preserve the database";
                return GrantResult::StorageError;
            }
        }
        aReceipt.Revision = existing.Revision;
        return GrantResult::Duplicate;
    }
    const auto* quest = FindQuest(acPlan.Quest);
    bool found{};
    QuestRunParticipants participants;
    if (!ReadQuestRunParticipants(acPlan.Quest, acPlan.Run, participants, found))
        return GrantResult::StorageError;
    if (!quest || quest->Data.Epoch != acPlan.Run || !found || participants.Initiator != acPlan.Initiator ||
        std::none_of(quest->Data.Events.begin(), quest->Data.Events.end(), [&](const auto& event)
        { return !event.Bootstrap && event.Status != 2 && event.Stage == acPlan.Stage; }))
    {
        m_error = "Quest grant lacks a matching accepted run, stage or known initiator";
        return GrantResult::Invalid;
    }
    QuestGrantReceipt receipt;
    receipt.Plan = acPlan; receipt.Participants = participants;
    WorldStore::Record record; record.Key = key;
    if (!Encode(receipt, record.Value))
    {
        m_error = "Quest grant effects cannot be encoded safely";
        return GrantResult::Invalid;
    }
    uint64_t revision{};
    // One immutable plan and both delivery indexes commit together. One event
    // creates exactly one inbox row per participant/revision, so a revision
    // cursor cannot cut off another reward in that participant's transaction.
    const Vector<WorldStore::Record> records{record,
        {Inbox(participants.Players[0]) + key, reference, 0}, {Inbox(participants.Players[1]) + key, reference, 0}};
    if (m_store.Commit(records, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return GrantResult::StorageError;
    }
    receipt.Revision = revision;
    aReceipt = std::move(receipt);
    return GrantResult::Committed;
}

bool State::ReadQuestGrants(const CharacterIdentity& acRecipient, uint64_t aAfterRevision, Vector<QuestGrantReceipt>& aReceipts) noexcept
{
    aReceipts.clear();
    if (!m_ready || !m_contract || !acRecipient.IsValid())
        return false;
    const auto prefix = Inbox(acRecipient);
    Vector<WorldStore::Record> indexes;
    if (!m_store.ReadPrefixAfterRevision(prefix, aAfterRevision, 64, indexes))
    {
        m_error = m_store.Error();
        return false;
    }
    for (const auto& index : indexes)
    {
        if (index.Value.empty() || index.Value.size() > 512 || std::find(index.Value.begin(), index.Value.end(), 0) != index.Value.end())
        {
            m_error = "Quest grant delivery index is invalid";
            aReceipts.clear(); return false;
        }
        const String key(reinterpret_cast<const char*>(index.Value.data()), index.Value.size());
        WorldStore::Record record;
        QuestGrantReceipt receipt;
        if (m_store.Read(key, record) != WorldStore::ReadResult::Found || record.Revision != index.Revision || !ReadValidQuestGrant(record, receipt) ||
            index.Key != prefix + key || (receipt.Participants.Players[0] != acRecipient && receipt.Participants.Players[1] != acRecipient))
        {
            m_error = "Cannot read a valid quest grant outbox; preserve the world database";
            aReceipts.clear(); return false;
        }
        receipt.Revision = record.Revision;
        aReceipts.push_back(std::move(receipt));
    }
    return true;
}
}
