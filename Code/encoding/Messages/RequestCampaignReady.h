#pragma once
#include "Message.h"
#include <Structs/QuestAdmissionDiff.h>

struct RequestCampaignReady final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCampaignReady;
    RequestCampaignReady() : ClientMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    enum Phase : uint8_t { Ready, Detail, Refused };
    uint8_t State{};
    uint64_t Nonce{};
    uint64_t Revision{};
    uint32_t SeedCount{}; // Baseline entries for Ready; total incompatible quests for Refused.
    QuestAdmissionDiff Diff;
};
