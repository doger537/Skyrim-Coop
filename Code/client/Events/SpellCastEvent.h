#pragma once

/**
 * @brief Dispatched when a spell has been cast locally.
 */
struct SpellCastEvent
{
    SpellCastEvent(uint32_t aCasterFormId, uint32_t aSpellId, uint32_t aDesiredTargetID, uint32_t aCastingSource, bool aIsDualCasting)
        : CasterFormId(aCasterFormId)
        , SpellId(aSpellId)
        , DesiredTargetID(aDesiredTargetID)
        , CastingSource(aCastingSource)
        , IsDualCasting(aIsDualCasting)
    {
    }

    uint32_t CasterFormId;
    uint32_t SpellId;
    uint32_t DesiredTargetID;
    uint32_t CastingSource;
    bool IsDualCasting;
};
