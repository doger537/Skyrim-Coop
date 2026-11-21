#pragma once
#include <Structs/WorldItemState.h>
#include <Structs/CharacterIdentity.h>

namespace Campaign
{
// Immutable inventory effect of one committed world-item lifecycle change.
// A native-save cursor, not a socket ACK, proves local application. Session is
// a fresh process/load nonce: an old receipt must not match a reused operation.
struct WorldItemReceipt
{
    WorldItemState State;
    CharacterIdentity Recipient;
    CharacterIdentity Session;
    uint64_t Operation{};
    int32_t Delta{};
    uint64_t ExpectedRevision{};
    bool IsValid() const noexcept
    {
        return State.IsValid() && State.Id <= State.Revision && State.Tick && Recipient.IsValid() && Session.IsValid() && Operation &&
            Delta && Delta >= -32767 && Delta <= 32767 && State.Item.Count <= 32767 && !State.Item.IsQuestItem &&
            !State.Item.IsWorn() && (Delta < 0 ? !ExpectedRevision && !State.Deleted && State.Id == State.Revision && State.Item.Count == -Delta :
                ExpectedRevision >= State.Id && ExpectedRevision < State.Revision &&
                (State.Deleted ? State.Item.Count == Delta : State.Item.Count <= 32767 - Delta));
    }
    Inventory::Entry InventoryDelta() const noexcept
    {
        auto item = State.Item; item.Count = Delta; return item;
    }
};
bool Encode(const WorldItemReceipt& acReceipt, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, WorldItemReceipt& aReceipt) noexcept;
}
