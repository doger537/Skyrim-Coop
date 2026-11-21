#pragma once
#include "Message.h"
#include <Structs/WorldItemState.h>
#include <Structs/CharacterIdentity.h>
#include <limits>
#include <algorithm>
#include <bit>

namespace WorldItemWire
{
inline constexpr size_t kSlots = 64, kReceiptBytes = 16384;
inline bool ReadBytes(TiltedPhoques::Buffer::Reader& r, TiltedPhoques::Vector<uint8_t>& out) noexcept
{
    uint64_t size{};
    out.clear();
    if (!r.ReadBits(size, 16) || size > kReceiptBytes)
        return false;
    for (uint64_t i = 0; i < size; ++i)
    {
        uint64_t value{};
        if (!r.ReadBits(value, 8))
            return false;
        out.push_back(static_cast<uint8_t>(value));
    }
    return true;
}
inline void WriteBytes(TiltedPhoques::Buffer::Writer& w, const TiltedPhoques::Vector<uint8_t>& data) noexcept
{
    w.WriteBits(data.size(), 16);
    for (uint8_t value : data)
        w.WriteBits(value, 8);
}
} // namespace WorldItemWire
struct RequestWorldItems final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestWorldItems;
    RequestWorldItems()
        : ClientMessage(Opcode)
    {
    }
    uint64_t Nonce{}, After{};
    bool Decoded{true};
    bool IsValid() const noexcept { return Decoded && Nonce && After <= uint64_t(INT64_MAX); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Nonce, 64);
        w.WriteBits(After, 64);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override { Decoded = r.ReadBits(Nonce, 64) && r.ReadBits(After, 64); }
};
struct RequestWorldItemTransfer final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestWorldItemTransfer;
    RequestWorldItemTransfer()
        : ClientMessage(Opcode)
    {
    }
    uint64_t Operation{}, Admission{};
    CharacterIdentity Session;
    int32_t Delta{};
    WorldItemState State; // Drop carries a provisional Id/Revision of 1; only server allocates the real ID.
    bool Decoded{true};
    bool IsValid() const noexcept
    {
        return Decoded && Operation && Admission && Session.IsValid() && Delta && Delta >= -32767 && Delta <= 32767 && State.IsValid() && !State.Deleted &&
               !State.Item.ContainsExtraData() && State.Item.Count <= 32767;
    }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Operation, 64);
        w.WriteBits(Admission, 64);
        Session.Serialize(w);
        w.WriteBits(static_cast<uint32_t>(Delta), 32);
        State.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t delta{};
        Decoded = r.ReadBits(Operation, 64) && r.ReadBits(Admission, 64) && Session.Deserialize(r) && r.ReadBits(delta, 32);
        Delta = std::bit_cast<int32_t>(static_cast<uint32_t>(delta));
        State.Deserialize(r);
        Decoded = Decoded && IsValid();
    }
};
struct NotifyWorldItems final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWorldItems;
    NotifyWorldItems()
        : ServerMessage(Opcode)
    {
    }
    uint64_t Nonce{}, After{}, Head{}, HostEpoch{};
    TiltedPhoques::Vector<WorldItemState> Items; // Full live set, including items in unloaded cells.
    TiltedPhoques::Vector<uint8_t> Receipt;      // At most the first inbox receipt after After. Never a client-minted effect.
    bool Decoded{true};
    bool IsValid() const noexcept
    {
        return Decoded && HostEpoch && Head <= uint64_t(INT64_MAX) && After <= Head && Items.size() <= WorldItemWire::kSlots && Receipt.size() <= WorldItemWire::kReceiptBytes &&
               (Nonce || (!After && Receipt.empty())) &&
               std::all_of(Items.begin(), Items.end(), [](const auto& v) { return v.IsValid() && !v.Deleted && !v.Item.ContainsExtraData() && v.Item.Count <= 32767; });
    }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        for (uint64_t v : {Nonce, After, Head, HostEpoch})
            w.WriteBits(v, 64);
        w.WriteBits(Items.size(), 8);
        for (const auto& v : Items)
            v.Serialize(w);
        WorldItemWire::WriteBytes(w, Receipt);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        Decoded = false;
        Items.clear();
        uint64_t count{};
        if (!r.ReadBits(Nonce, 64) || !r.ReadBits(After, 64) || !r.ReadBits(Head, 64) || !r.ReadBits(HostEpoch, 64) || !r.ReadBits(count, 8) || count > WorldItemWire::kSlots)
            return;
        for (uint64_t i = 0; i < count; ++i)
        {
            WorldItemState item;
            item.Deserialize(r);
            if (!item.IsValid())
                return;
            Items.push_back(std::move(item));
        }
        if (!WorldItemWire::ReadBytes(r, Receipt))
            return;
        Decoded = true;
        Decoded = IsValid();
    }
};
struct NotifyWorldItemTransfer final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWorldItemTransfer;
    NotifyWorldItemTransfer()
        : ServerMessage(Opcode)
    {
    }
    uint64_t Operation{}, Admission{}, Revision{};
    bool Accepted{}, Decoded{true};
    bool IsValid() const noexcept { return Decoded && Operation && Admission && (Accepted == (Revision != 0)); }
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Operation, 64);
        w.WriteBits(Admission, 64);
        w.WriteBits(Revision, 64);
        w.WriteBits(Accepted, 1);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t v{};
        Decoded = r.ReadBits(Operation, 64) && r.ReadBits(Admission, 64) && r.ReadBits(Revision, 64) && r.ReadBits(v, 1);
        Accepted = v != 0;
    }
};
struct RequestWorldItemMotion final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestWorldItemMotion;
    RequestWorldItemMotion()
        : ClientMessage(Opcode)
    {
    }
    WorldItemMotion State;
    bool Interaction{}, Decoded{true};
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Interaction, 1);
        State.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t v{};
        Decoded = r.ReadBits(v, 1);
        Interaction = v != 0;
        State.Deserialize(r);
    }
};
struct NotifyWorldItemMotion final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWorldItemMotion;
    NotifyWorldItemMotion()
        : ServerMessage(Opcode)
    {
    }
    WorldItemMotion State;
    bool Interaction{}, Decoded{true};
    void SerializeRaw(TiltedPhoques::Buffer::Writer& w) const noexcept override
    {
        w.WriteBits(Interaction, 1);
        State.Serialize(w);
    }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& r) noexcept override
    {
        uint64_t v{};
        Decoded = r.ReadBits(v, 1);
        Interaction = v != 0;
        State.Deserialize(r);
    }
};
