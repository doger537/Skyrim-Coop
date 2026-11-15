#pragma once
#include <World.h>
#include <Components.h>
#include <Structs/ObjectData.h>

inline bool GetPersistentObject(const World& acWorld, entt::entity aEntity, ObjectData& aData) noexcept
{
    if (!acWorld.valid(aEntity))
        return false;
    const auto* pForm = acWorld.try_get<FormIdComponent>(aEntity);
    const auto* pObject = acWorld.try_get<ObjectComponent>(aEntity);
    const auto* pInventory = acWorld.try_get<InventoryComponent>(aEntity);
    const auto* pCell = acWorld.try_get<CellIdComponent>(aEntity);
    if (!pForm || !pObject || !pInventory || !pCell)
        return false;
    aData = {};
    aData.Id = pForm->Id;
    aData.CellId = pCell->Cell;
    aData.WorldSpaceId = pCell->WorldSpaceId;
    aData.CurrentCoords = pCell->CenterCoords;
    aData.CurrentLockData = pObject->CurrentLockData;
    aData.CurrentInventory = pInventory->Content;
    return true;
}
