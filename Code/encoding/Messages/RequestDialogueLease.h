#pragma once
#include "Message.h"

struct RequestDialogueLease final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestDialogueLease;
    static constexpr uint64_t kDurationMs = 3000;
    RequestDialogueLease() : ClientMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool IsValid() const noexcept { return Decoded && ServerId != UINT32_MAX && OwnershipEpoch != 0 && Sequence != 0; }
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    uint64_t Sequence{};
    uint64_t Tick{};
    bool Active{};
    bool Decoded{true};
};
