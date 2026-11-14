#pragma once
#include "Message.h"
#include <Structs/QuestJournal.h>

struct RequestCampaignSeed final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCampaignSeed;
    RequestCampaignSeed() : ClientMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    uint64_t Nonce{};
    QuestJournal Data;
};
