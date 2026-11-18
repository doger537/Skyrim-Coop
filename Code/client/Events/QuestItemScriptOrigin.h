#pragma once
#include <TiltedCore/Stl.hpp>

// Local diagnostic provenance, not a stable network reward ID. VM stack IDs,
// instruction offsets and runtime FormIDs must not identify durable receipts.
struct QuestItemScriptOrigin
{
    enum class Source : uint8_t { Quest, Alias, TopicInfo, Scene };
    Source Kind{};
    uint32_t QuestFormId{};
    uint32_t StackId{};
    uint32_t InstructionOffset{};
    TiltedPhoques::String Script;
    TiltedPhoques::String Function;
};
