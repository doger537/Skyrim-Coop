#pragma once
#include <Structs/CharacterIdentity.h>
#include <array>

namespace Campaign
{
// Saved with the first accepted event of a NEW quest run. Imported journals do
// not identify who took an old quest, so they must never fabricate an owner.
struct QuestRunParticipants
{
    CharacterIdentity Initiator;
    std::array<CharacterIdentity, 2> Players;
    bool IsValid() const noexcept
    {
        return Initiator.IsValid() && Players[0].IsValid() && Players[1].IsValid() && Players[0] != Players[1] &&
            (Initiator == Players[0] || Initiator == Players[1]);
    }
    bool operator==(const QuestRunParticipants&) const noexcept = default;
};
}
