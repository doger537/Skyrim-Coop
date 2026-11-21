#pragma once
#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

// Transient conversation intent, deliberately absent from campaign/save records.
struct DialogueLeaseComponent
{
    uint32_t SpeakerPlayerId{};
    uint32_t OwnerPlayerId{};
    uint32_t OwnershipEpoch{};
    uint64_t Deadline{};
};
