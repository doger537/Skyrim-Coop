#pragma once

#include <Structs/Inventory.h>
#include <ExtraData/ExtraDataList.h>
#include <Events/QuestItemScriptOrigin.h>
#include <optional>

/**
 * @brief Dispatched when the contents of an object or actor inventory changes locally.
 *
 * The event has a Drop member variable, since dropped items need to be handled differently.
 */
struct InventoryChangeEvent
{
    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem)
        : FormId(aFormId)
        , Item(std::move(arItem))
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop, bool aUpdateClients)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
        , UpdateClients(aUpdateClients)
    {
    }

    uint32_t FormId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    // The item moved to or from the local player, e.g. looting or pickpocketing an actor owned by someone else
    bool IsPlayerTransfer = false;
    // Local-only handle from the completed native drop. Resolve it on the game
    // runner; neither an engine pointer nor a runtime FormID is a network identity.
    uint32_t DroppedReferenceHandle{};
    // Copied while the delayed Papyrus AddItem actually executes. Ordinary
    // loot/purchases and remote inventory replay do not acquire this context.
    std::optional<QuestItemScriptOrigin> ScriptOrigin;
};
