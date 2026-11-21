#pragma once
#include <array>
#include <cstdint>
struct TESObjectCELL;
namespace PhysicalCellScan
{
struct Cursor
{
    uint32_t Cell{}, Index{}, Capacity{};
    uintptr_t Storage{};
};
struct Batch
{
    std::array<uint32_t, 16> Handles{};
    uint32_t Count{};
    bool Complete{}, Busy{}, Invalid{};
};
// Exact 1.7.104 cell table/lock verified via MCP. One CAS, no spin/wait;
// retain table entries under the lock, resolve handles after releasing it.
Batch Next(TESObjectCELL* apCell, Cursor& aCursor) noexcept;
} // namespace PhysicalCellScan
