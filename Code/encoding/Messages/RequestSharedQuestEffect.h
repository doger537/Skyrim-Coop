#pragma once
#include "Message.h"
struct RequestSharedQuestEffect final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestSharedQuestEffect;
    enum Phase : uint8_t { Reserve, Commit, Cancel };
    RequestSharedQuestEffect() : ClientMessage(Opcode) {}
    uint64_t Nonce{}, Admission{};
    uint8_t State{};
    TiltedPhoques::Vector<uint8_t> Payload;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Nonce && Admission && State <= Cancel && Payload.size() <= 16384 && (State == Cancel ? Payload.empty() : !Payload.empty()); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Nonce, 64); w.WriteBits(Admission, 64); w.WriteBits(State, 8); w.WriteBits(Payload.size(), 16);
        for (uint8_t value : Payload) w.WriteBits(value, 8);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        Decoded = false; Payload.clear(); uint64_t state{}, size{};
        if (!r.ReadBits(Nonce,64) || !r.ReadBits(Admission,64) || !r.ReadBits(state,8) || !r.ReadBits(size,16) || size > 16384) return;
        State = static_cast<uint8_t>(state); Payload.reserve(static_cast<size_t>(size));
        for (uint64_t i=0;i<size;++i) { uint64_t value{}; if (!r.ReadBits(value,8)) { Payload.clear(); return; } Payload.push_back(static_cast<uint8_t>(value)); }
        Decoded = true; Decoded = IsValid();
    }
};
