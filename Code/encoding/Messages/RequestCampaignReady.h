#pragma once
#include "Message.h"
#include <Structs/QuestJournal.h>

struct RequestCampaignReady final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCampaignReady;
    RequestCampaignReady() : ClientMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    uint64_t Nonce{};
    uint64_t Revision{};
    uint32_t SeedCount{};
};
