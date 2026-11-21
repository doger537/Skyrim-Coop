#include <TiltedOnlinePCH.h>
#include "PhysicalCellScan.h"
#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>
#include <AI/NativeActorPose.h>

namespace PhysicalCellScan
{
Batch Next(TESObjectCELL* apCell, Cursor& aCursor) noexcept
{
    Batch result;
    if (!NativeActorPose::OnRunner() || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0" || !apCell || !apCell->IsAttached())
    {
        result.Invalid = true;
        return result;
    }
    static_assert(offsetof(TESObjectCELL, lock) == 0x120);
    static_assert(offsetof(TESObjectCELL::ReferenceData, refArray) == 0x28);
    auto& lock = apCell->lock;
    const uint32_t thread = GetCurrentThreadId();
    // MCP 1401B4D80: recursive increment, otherwise CAS(count,1,0),
    // then publish owner. Unlike the native blocking entry, never retry here.
    if (lock.OwningThread == thread)
        InterlockedIncrement(&lock.uiLockCount);
    else if (InterlockedCompareExchange(&lock.uiLockCount, 1, 0) != 0)
    {
        result.Busy = true;
        return result;
    }
    else
    {
        lock.OwningThread = thread;
        _mm_sfence();
    }
    std::array<TESObjectREFR*, 16> retained{};
    const auto& table = apCell->refData;
    const uintptr_t storage = reinterpret_cast<uintptr_t>(table.refArray);
    if (table.capacity > 1048576 || table.available > table.capacity || (table.capacity && !storage))
        result.Invalid = true;
    else
    {
        if (aCursor.Cell != apCell->formID || aCursor.Storage != storage || aCursor.Capacity != table.capacity)
            aCursor = {apCell->formID, 0, table.capacity, storage};
        for (uint32_t visited = 0; visited < 64 && aCursor.Index < table.capacity && result.Count < retained.size(); ++visited)
        {
            auto* ref = table.refArray[aCursor.Index++].Get();
            if (ref)
            {
                InterlockedIncrement(&ref->handleRefObject.refCount);
                retained[result.Count++] = ref;
            }
        }
        result.Complete = aCursor.Index == table.capacity;
    }
    // MCP 1402CBEF0: this exact cell lock protects insertion/removal at +88.
    lock.Unlock();
    for (uint32_t i = 0; i < result.Count; ++i)
    {
        auto* ref = retained[i];
        if (!ref->IsDeleted() && !(ref->flags & TESForm::DISABLED) && ref->baseForm)
            result.Handles[i] = ref->GetHandle().handle.iBits;
        ref->handleRefObject.DecRefHandle();
    }
    return result;
}
} // namespace PhysicalCellScan
