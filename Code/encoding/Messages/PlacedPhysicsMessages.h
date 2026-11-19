#pragma once
#include "Message.h"
#include <Structs/PlacedPhysicsState.h>
struct RequestPlacedPhysics final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPlacedPhysics;
    enum Phase : uint8_t
    {
        Observe,
        Motion,
        Interaction,
        Gone,
        Coverage
    };
    RequestPlacedPhysics()
        : ClientMessage(Opcode)
    {
    }
    uint8_t Action{};
    PlacedPhysicsState State;
    TiltedPhoques::Vector<GameId> Cells;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Action <= Coverage && (Action == Coverage ? Cells.size() <= 121 : State.IsValid()); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Action, 8);
        if (Action != Coverage)
            State.Serialize(w);
        else
        {
            w.WriteBits(Cells.size(), 8);
            for (auto id : Cells)
            {
                w.WriteBits(id.ModId, 32);
                w.WriteBits(id.BaseId, 32);
            }
        }
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        Decoded = false;
        uint64_t action{}, count{};
        Cells.clear();
        if (!r.ReadBits(action, 8) || action > Coverage)
            return;
        Action = static_cast<uint8_t>(action);
        if (Action != Coverage)
        {
            if (!State.Deserialize(r))
                return;
        }
        else
        {
            if (!r.ReadBits(count, 8) || count > 121)
                return;
            for (uint64_t i = 0; i < count; ++i)
            {
                uint64_t m{}, b{};
                if (!r.ReadBits(m, 32) || !r.ReadBits(b, 32))
                    return;
                GameId id(static_cast<uint32_t>(m), static_cast<uint32_t>(b));
                if (!id)
                    return;
                Cells.push_back(id);
            }
        }
        Decoded = true;
        Decoded = IsValid();
    }
};
struct NotifyPlacedPhysics final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlacedPhysics;
    enum Phase : uint8_t
    {
        Authority,
        Motion,
        Interaction
    };
    NotifyPlacedPhysics()
        : ServerMessage(Opcode)
    {
    }
    uint8_t Action{};
    PlacedPhysicsState State;
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Action <= Interaction && State.IsValid(); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Action, 8);
        State.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t action{};
        Decoded = r.ReadBits(action, 8);
        Action = static_cast<uint8_t>(action);
        if (!State.Deserialize(r))
            Decoded = false;
        Decoded = IsValid();
    }
};
