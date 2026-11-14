#pragma once
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <Structs/Mods.h>
#include <Structs/ObjectData.h>

namespace Campaign
{
struct Contract
{
    TiltedPhoques::String Runtime;
    Mods Content;
};

struct QuestEvent
{
    uint8_t Status{};
    uint16_t Stage{};
    bool operator==(const QuestEvent&) const noexcept = default;
};

struct Decision
{
    TiltedPhoques::String Group;
    uint16_t Stage{};
    bool operator==(const Decision&) const noexcept = default;
};

struct Quest
{
    GameId Id{};
    uint32_t Epoch{1};
    uint8_t Type{};
    bool Started{};
    bool Stopped{};
    TiltedPhoques::Vector<QuestEvent> Events;
    TiltedPhoques::Vector<Decision> Decisions;
};

// Separate from the network serializer: corrupted counts and truncated records must
// fail before allocation or applying anything to the world.
bool Encode(const Contract& acContract, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, Contract& aContract) noexcept;
bool Encode(const ObjectData& acObject, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, ObjectData& aObject) noexcept;
bool Encode(const Quest& acQuest, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept;
bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, Quest& aQuest) noexcept;
} // namespace Campaign
