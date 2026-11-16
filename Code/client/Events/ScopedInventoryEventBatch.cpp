#include <TiltedOnlinePCH.h>
#include "ScopedInventoryEventBatch.h"
#include "InventoryChangeEvent.h"
#include <World.h>

namespace
{
thread_local ScopedInventoryEventBatch* s_pBatch{};
}

ScopedInventoryEventBatch::ScopedInventoryEventBatch() noexcept
    : m_pPrevious(s_pBatch)
{
    s_pBatch = this;
}

ScopedInventoryEventBatch::~ScopedInventoryEventBatch() noexcept
{
    s_pBatch = m_pPrevious;
    if (!m_published)
        for (auto& event : m_equipment)
            QueueEquipment(std::move(event));
}

void ScopedInventoryEventBatch::QueueEquipment(EquipmentChangeEvent aEvent) noexcept
{
    if (s_pBatch)
        s_pBatch->m_equipment.push_back(std::move(aEvent));
    else
        World::Get().GetRunner().Trigger(std::move(aEvent));
}

void ScopedInventoryEventBatch::Publish(InventoryChangeEvent aEvent) noexcept
{
    TP_ASSERT(!m_published, "An inventory event batch must be published once");
    m_published = true;
    World::Get().GetRunner().QueueGameplay([event = std::move(aEvent), equipment = std::move(m_equipment)]() mutable
    {
        auto& dispatcher = World::Get().GetDispatcher();
        dispatcher.trigger(std::move(event));
        for (auto& change : equipment)
            dispatcher.trigger(std::move(change));
    });
}
