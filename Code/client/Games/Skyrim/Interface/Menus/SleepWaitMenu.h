#pragma once
#include <Interface/IMenu.h>

struct SleepWaitMenu : IMenu
{
    static uint64_t Session() noexcept;
    static bool IsPending(uint64_t aSession, bool aSleeping) noexcept;
    static bool BeginApproved(uint64_t aSession, uint8_t aHours, bool aSleeping) noexcept;

    float timer;
    uint32_t pad34;
    GFxValue root;
    bool isSleeping;
    bool isActive;
    uint16_t pad52;
    uint32_t pad54;
};
static_assert(sizeof(SleepWaitMenu) == 0x58);
static_assert(offsetof(SleepWaitMenu, timer) == 0x30);
static_assert(offsetof(SleepWaitMenu, isSleeping) == 0x50);
static_assert(offsetof(SleepWaitMenu, isActive) == 0x51);
