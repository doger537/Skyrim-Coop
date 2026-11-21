#include "PhysicsFrame.h"
#include <TiltedCore/ViewBuffer.hpp>
#include <bit>
#include <cmath>
#include <algorithm>
bool PhysicsFrameEntry::IsValid() const noexcept
{
    return Type <= Actor && SourceSlot < 2 && PresentMask && PresentMask <= 3 && (PresentMask & (1 << SourceSlot)) && Cell && Rig.IsValid() &&
           (Type == Placed ? bool(Reference) && !Id : !Reference && Epoch && (Type == Drop ? Id != 0 : Id < UINT32_MAX && Epoch <= UINT32_MAX));
}
bool PhysicsFrameEntry::SameKey(const PhysicsFrameEntry& other) const noexcept
{
    return Type == other.Type && (Type == Placed ? Reference == other.Reference : Id == other.Id);
}
bool PhysicsFrameEntry::Less(const PhysicsFrameEntry& a, const PhysicsFrameEntry& b) noexcept
{
    return a.Type != b.Type ? a.Type < b.Type : a.Type == Placed ? a.Reference.LogFormat() < b.Reference.LogFormat() : a.Id < b.Id;
}
void PhysicsFrameEntry::Serialize(TiltedPhoques::Buffer::Writer& w) const noexcept
{
    w.WriteBits(Type, 8);
    w.WriteBits(SourceSlot, 8);
    w.WriteBits(PresentMask, 8);
    for (auto id : {Reference, Cell, WorldSpace})
    {
        w.WriteBits(id.ModId, 32);
        w.WriteBits(id.BaseId, 32);
    }
    w.WriteBits(Id, 64);
    w.WriteBits(Epoch, 64);
    Rig.Serialize(w);
}
bool PhysicsFrameEntry::Deserialize(TiltedPhoques::Buffer::Reader& r) noexcept
{
    uint64_t type{}, slot{}, mask{};
    if (!r.ReadBits(type, 8) || !r.ReadBits(slot, 8) || !r.ReadBits(mask, 8))
        return false;
    Type = static_cast<uint8_t>(type);
    SourceSlot = static_cast<uint8_t>(slot);
    PresentMask = static_cast<uint8_t>(mask);
    for (auto* id : {&Reference, &Cell, &WorldSpace})
    {
        uint64_t m{}, b{};
        if (!r.ReadBits(m, 32) || !r.ReadBits(b, 32))
            return false;
        *id = GameId(static_cast<uint32_t>(m), static_cast<uint32_t>(b));
    }
    return r.ReadBits(Id, 64) && r.ReadBits(Epoch, 64) && Rig.Deserialize(r) && IsValid();
}
bool PhysicsFrame::IsValid() const noexcept
{
    if (!Checkpoint || !std::isfinite(NativeToWorld) || NativeToWorld <= 0.f || NativeToWorld > 10000.f || Entries.size() > kMaxEntries)
        return false;
    for (size_t i = 0; i < Entries.size(); ++i)
        if (!Entries[i].IsValid() || (i && !PhysicsFrameEntry::Less(Entries[i - 1], Entries[i])))
            return false;
    return true;
}
bool PhysicsFrame::Encode(TiltedPhoques::Vector<uint8_t>& bytes) const noexcept
{
    if (!IsValid())
        return false;
    TiltedPhoques::Buffer buffer(kMaxBytes);
    TiltedPhoques::Buffer::Writer w(&buffer);
    w.WriteBits(1, 16);
    w.WriteBits(Checkpoint, 64);
    w.WriteBits(std::bit_cast<uint32_t>(NativeToWorld), 32);
    w.WriteBits(Entries.size(), 32);
    for (const auto& entry : Entries)
        entry.Serialize(w);
    if (w.Size() > kMaxBytes)
        return false;
    bytes.assign(buffer.GetWriteData(), buffer.GetWriteData() + w.Size());
    return true;
}
bool PhysicsFrame::Decode(const TiltedPhoques::Vector<uint8_t>& bytes, PhysicsFrame& frame) noexcept
{
    if (bytes.empty() || bytes.size() > kMaxBytes)
        return false;
    TiltedPhoques::ViewBuffer view(const_cast<uint8_t*>(bytes.data()), bytes.size());
    TiltedPhoques::Buffer::Reader r(&view);
    uint64_t version{}, count{}, scale{};
    PhysicsFrame result;
    if (!r.ReadBits(version, 16) || version != 1 || !r.ReadBits(result.Checkpoint, 64) || !r.ReadBits(scale, 32) || !r.ReadBits(count, 32) || count > kMaxEntries)
        return false;
    result.NativeToWorld = std::bit_cast<float>(static_cast<uint32_t>(scale));
    result.Entries.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i)
    {
        PhysicsFrameEntry entry;
        if (!entry.Deserialize(r))
            return false;
        result.Entries.push_back(std::move(entry));
    }
    TiltedPhoques::Vector<uint8_t> encoded;
    if (!result.Encode(encoded) || encoded != bytes)
        return false;
    frame = std::move(result);
    return true;
}
