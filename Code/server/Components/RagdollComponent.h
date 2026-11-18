#pragma once
#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif
#include <Structs/RagdollSnapshot.h>
struct RagdollComponent
{
    uint32_t OwnershipEpoch{};
    uint64_t Sequence{}, Tick{}, ReceivedAt{};
    uint32_t Interactor{};
    uint64_t InteractionSequence{}, InteractionAt{};
    RagdollSnapshot Latest;
};
