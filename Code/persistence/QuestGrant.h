#pragma once
#include "QuestRunParticipants.h"
#include <Structs/Inventory.h>
#include <algorithm>

namespace Campaign
{
struct QuestGrantItem
{
    // Roles come from a verified adapter/custody decision. AddItem's presence
    // in a quest script alone is not evidence of a final reward.
    enum : uint8_t { Key, Reward, Shared };
    uint8_t Role{};
    Inventory::Entry Item;
    bool operator==(const QuestGrantItem&) const noexcept = default;
};
struct QuestGrantPlan
{
    static constexpr size_t kMaxItems = 32;
    GameId Quest{};
    uint32_t Run{};
    uint16_t Stage{};
    CharacterIdentity Initiator;
    // Stable adapter event/occurrence identity; never a VM stack/socket ID.
    TiltedPhoques::String Event;
    TiltedPhoques::Vector<QuestGrantItem> Items;
    bool Shared() const noexcept
    {
        return !Items.empty() && std::all_of(Items.begin(), Items.end(), [](const auto& value) { return value.Role == QuestGrantItem::Shared; });
    }
    bool IsValid() const noexcept
    {
        return Run && Initiator.IsValid() && !Event.empty() && Event.size() <= 96 &&
            std::all_of(Event.begin(), Event.end(), [](char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-' || c == '.' || c == ':';
            }) && !Items.empty() && Items.size() <= kMaxItems &&
            std::all_of(Items.begin(), Items.end(), [](const auto& value)
            {
                return value.Role == QuestGrantItem::Shared ? value.Item.Count >= -1000000 && value.Item.Count <= 1000000 &&
                    value.Item.BaseId && !value.Item.ContainsExtraData() :
                    value.Role <= QuestGrantItem::Reward && value.Item.Count > 0 && value.Item.Count <= 1000000 &&
                    value.Item.IsQuestItem == (value.Role == QuestGrantItem::Key);
            }) && (Shared() || std::none_of(Items.begin(), Items.end(), [](const auto& value) { return value.Role == QuestGrantItem::Shared; }));
    }
    bool operator==(const QuestGrantPlan&) const noexcept = default;
};
struct QuestGrantReceipt
{
    QuestGrantPlan Plan;
    QuestRunParticipants Participants;
    uint64_t Revision{};
    // Durable delivery intent, not proof Skyrim applied it. A network ACK is
    // insufficient to delete it; a saved client receipt/checkpoint is required.
    Inventory For(const CharacterIdentity& acRecipient) const noexcept
    {
        Inventory inventory;
        if (acRecipient != Participants.Players[0] && acRecipient != Participants.Players[1])
            return inventory;
        for (const auto& value : Plan.Items)
            if (value.Role == QuestGrantItem::Shared || value.Role == QuestGrantItem::Reward || acRecipient == Participants.Initiator)
            {
                auto item = value.Item;
                item.ExtraWorn = item.ExtraWornLeft = false;
                inventory.Entries.push_back(std::move(item));
            }
        return inventory;
    }
};
bool Encode(const QuestGrantReceipt& acReceipt, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, QuestGrantReceipt& aReceipt) noexcept;
bool Encode(const QuestGrantPlan& acPlan, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, QuestGrantPlan& aPlan) noexcept;
}
