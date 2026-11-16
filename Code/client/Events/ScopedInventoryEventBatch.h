#pragma once
#include <TiltedCore/Stl.hpp>
#include "EquipmentChangeEvent.h"

struct InventoryChangeEvent;

// A native drop may unequip an item while it is executing. Keep those events
// behind the resulting inventory delta, without publishing an unfinished drop.
class ScopedInventoryEventBatch
{
public:
    ScopedInventoryEventBatch() noexcept;
    ~ScopedInventoryEventBatch() noexcept;
    TP_NOCOPYMOVE(ScopedInventoryEventBatch);

    static void QueueEquipment(EquipmentChangeEvent aEvent) noexcept;
    void Publish(InventoryChangeEvent aEvent) noexcept;

private:
    ScopedInventoryEventBatch* m_pPrevious{};
    TiltedPhoques::Vector<EquipmentChangeEvent> m_equipment;
    bool m_published{};
};
