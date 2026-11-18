#pragma once
#include "Message.h"

struct RequestQuestDecision final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestDecision;
    RequestQuestDecision() : ClientMessage(Opcode) {}
    uint64_t Proposal{};
    bool Accepted{};
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Proposal; }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
