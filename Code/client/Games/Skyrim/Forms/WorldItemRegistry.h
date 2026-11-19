#pragma once
#include <TiltedCore/Stl.hpp>
#include <cstdint>
struct TESObjectREFR;
namespace WorldItemRegistry
{
struct Mapping { uint64_t Id{}; uint32_t Handle{}, FormId{}; };
// Runner only. Read slots incrementally after native load; do not turn a scan
// of all saved objects into one render-frame operation.
bool ReadSlot(uint32_t aSlot, Mapping& aMapping) noexcept;
bool ContainsReference(TESObjectREFR* apReference) noexcept;
bool Bind(uint32_t aSlot, uint64_t aId, TESObjectREFR* apReference) noexcept;
bool Clear(uint32_t aSlot, uint64_t aExpectedId) noexcept;
bool ReadReceipt(uint64_t& aCursor, bool& aBound) noexcept;
bool BindReceipt() noexcept;
bool AdvanceReceipt(uint64_t aExpected, uint64_t aNext) noexcept;
bool IsWriting() noexcept;
bool HasPending() noexcept;
void AfterLoad() noexcept; // atomic-only reset after an actual native load
}
