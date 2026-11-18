#pragma once
#include "Message.h"
#include <Structs/GameId.h>

struct NotifyQuestProposal final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestProposal;
    NotifyQuestProposal() : ServerMessage(Opcode) {}
    uint64_t Proposal{};
    GameId Id{};
    uint32_t Epoch{};
    uint16_t Stage{};
    uint8_t Status{};
    uint8_t QuestType{};
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Proposal && Id.BaseId && Epoch && Status <= 2 && QuestType <= 11; }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
