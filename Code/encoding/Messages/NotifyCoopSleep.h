#pragma once
#include "Message.h"
#include <common/CoopSleepVote.h>
#include <Structs/TimeModel.h>

struct NotifyCoopSleep final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCoopSleep;
    NotifyCoopSleep() : ServerMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool IsValid() const noexcept;

    CoopSleepVote::Phase Phase{CoopSleepVote::Phase::Idle};
    std::array<uint32_t, 2> Members{};
    uint64_t Round{};
    uint64_t Deadline{};
    uint64_t Tick{};
    uint8_t Hours{};
    uint8_t Votes{};
    TimeModel Time{}; // Only serialized for the commit, applied before native rest starts.
    bool Decoded{true};
};
