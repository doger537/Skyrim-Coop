#pragma once
#include "Message.h"
#include <Structs/PhysicsFrame.h>
#include <bit>
struct PhysicsFramePayload
{
    uint64_t Id{}, Nonce{};
    uint32_t Index{};
    float NativeToWorld{};
    PhysicsFrame::Digest Digest{};
    TiltedPhoques::Vector<PhysicsFrameEntry> Entries;
    bool Decoded{true};
    bool IsValid() const noexcept;
    void Serialize(TiltedPhoques::Buffer::Writer&) const noexcept;
    bool Deserialize(TiltedPhoques::Buffer::Reader&) noexcept;
};
struct RequestPhysicsFrame final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPhysicsFrame;
    enum Phase : uint8_t
    {
        Offer,
        End,
        Applied,
        Failed
    };
    RequestPhysicsFrame()
        : ClientMessage(Opcode)
    {
    }
    uint8_t Action{};
    PhysicsFramePayload Data;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Action <= Failed && Data.IsValid() && (Action == Offer ? !Data.Entries.empty() : Data.Entries.empty()); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Action, 8);
        Data.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t action{};
        Decoded = r.ReadBits(action, 8);
        Action = static_cast<uint8_t>(action);
        Decoded = Decoded && Data.Deserialize(r);
        Decoded = IsValid();
    }
};
struct NotifyPhysicsFrame final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPhysicsFrame;
    enum Phase : uint8_t
    {
        Canonical,
        End
    };
    NotifyPhysicsFrame()
        : ServerMessage(Opcode)
    {
    }
    uint8_t Action{};
    PhysicsFramePayload Data;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Action <= End && Data.IsValid() && (Action == Canonical ? !Data.Entries.empty() : Data.Entries.empty()); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Action, 8);
        Data.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t action{};
        Decoded = r.ReadBits(action, 8);
        Action = static_cast<uint8_t>(action);
        Decoded = Decoded && Data.Deserialize(r);
        Decoded = IsValid();
    }
};
