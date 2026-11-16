#pragma once
#include "Message.h"

struct NotifyDialogueLease final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDialogueLease;
    NotifyDialogueLease() : ServerMessage(Opcode) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool IsValid() const noexcept { return Decoded && ServerId != UINT32_MAX && OwnershipEpoch != 0 && Revision != 0 && (Active == (Deadline != 0)); }
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    uint32_t SpeakerPlayerId{};
    uint64_t Revision{};
    uint64_t Deadline{};
    bool Active{};
    bool Decoded{true};
};
