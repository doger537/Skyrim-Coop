#include "PhysicsFrameMessages.h"
#include <cmath>
#include <algorithm>
bool PhysicsFramePayload::IsValid() const noexcept
{
    return Decoded && Id && Nonce && Index <= PhysicsFrame::kMaxEntries && Entries.size() <= 8 && Index + Entries.size() <= PhysicsFrame::kMaxEntries &&
           std::isfinite(NativeToWorld) && NativeToWorld > 0.f && NativeToWorld <= 10000.f &&
           std::all_of(Entries.begin(), Entries.end(), [](const auto& e) { return e.IsValid(); });
}
void PhysicsFramePayload::Serialize(TiltedPhoques::Buffer::Writer& w) const noexcept
{
    w.WriteBits(Id, 64);
    w.WriteBits(Nonce, 64);
    w.WriteBits(Index, 32);
    w.WriteBits(std::bit_cast<uint32_t>(NativeToWorld), 32);
    for (auto value : Digest)
        w.WriteBits(value, 8);
    w.WriteBits(Entries.size(), 8);
    for (const auto& entry : Entries)
        entry.Serialize(w);
}
bool PhysicsFramePayload::Deserialize(TiltedPhoques::Buffer::Reader& r) noexcept
{
    Decoded = false;
    uint64_t index{}, scale{}, count{};
    Entries.clear();
    if (!r.ReadBits(Id, 64) || !r.ReadBits(Nonce, 64) || !r.ReadBits(index, 32) || !r.ReadBits(scale, 32))
        return false;
    Index = static_cast<uint32_t>(index);
    NativeToWorld = std::bit_cast<float>(static_cast<uint32_t>(scale));
    for (auto& byte : Digest)
    {
        uint64_t value{};
        if (!r.ReadBits(value, 8))
            return false;
        byte = static_cast<uint8_t>(value);
    }
    if (!r.ReadBits(count, 8) || count > 8)
        return false;
    for (uint64_t i = 0; i < count; ++i)
    {
        PhysicsFrameEntry entry;
        if (!entry.Deserialize(r))
            return false;
        Entries.push_back(std::move(entry));
    }
    Decoded = true;
    return Decoded = IsValid();
}
