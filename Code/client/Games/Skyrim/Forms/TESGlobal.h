#pragma once

#include <Forms/TESForm.h>
#include <Misc/BSString.h>

struct TESGlobal : TESForm
{
    // Runner thread only, after resolving a validated TrueCoop-owned global.
    bool SetSavedWord(uint16_t aValue) noexcept;

    BSString unk14;
    uint8_t unk1C;
    uint8_t pad[3];
    union
    {
        uint32_t i;
        float f;
    };
};

static_assert(offsetof(TESGlobal, f) == 0x34);
static_assert(offsetof(TESGlobal, unk1C) == 0x30);
