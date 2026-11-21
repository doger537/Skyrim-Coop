#pragma once
#include "Message.h"
#include <common/CoopSleepVote.h>

struct RequestCoopSleep final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCoopSleep;
    RequestCoopSleep() : ClientMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool IsValid() const noexcept;

    CoopSleepVote::Action Action{CoopSleepVote::Action::Vote};
    uint64_t Sequence{};
    uint64_t Round{};
    uint64_t Tick{};
    uint8_t Hours{};
    bool Decoded{true};
};
