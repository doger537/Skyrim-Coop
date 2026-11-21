#pragma once
#include "Message.h"
struct NotifySharedQuestEffect final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifySharedQuestEffect;
    enum Phase : uint8_t { Permitted, Committed, Delivered, Rejected };
    NotifySharedQuestEffect() : ServerMessage(Opcode) {}
    uint64_t Nonce{}, Revision{};
    uint8_t State{};
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && State <= Rejected && (Nonce || State == Committed) && (State == Committed || State == Delivered ? Revision != 0 : Revision == 0); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override { w.WriteBits(Nonce,64); w.WriteBits(Revision,64); w.WriteBits(State,8); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    { Decoded = false; uint64_t state{}; if (!r.ReadBits(Nonce,64) || !r.ReadBits(Revision,64) || !r.ReadBits(state,8)) return; State=static_cast<uint8_t>(state); Decoded=true; Decoded=IsValid(); }
};
