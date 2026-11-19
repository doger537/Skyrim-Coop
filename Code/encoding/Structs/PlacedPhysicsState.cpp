#include "PlacedPhysicsState.h"
void PlacedPhysicsState::Serialize(TiltedPhoques::Buffer::Writer& w) const noexcept
{
    for (const auto& id : {Reference, Cell, WorldSpace})
    {
        w.WriteBits(id.ModId, 32);
        w.WriteBits(id.BaseId, 32);
    }
    w.WriteBits(Owner, 32);
    for (uint64_t value : {Epoch, Sequence, Tick})
        w.WriteBits(value, 64);
    w.WriteBits(Retired, 1);
    Rig.Serialize(w);
}
bool PlacedPhysicsState::Deserialize(TiltedPhoques::Buffer::Reader& r) noexcept
{
    Decoded = false;
    for (auto* id : {&Reference, &Cell, &WorldSpace})
    {
        uint64_t mod{}, base{};
        if (!r.ReadBits(mod, 32) || !r.ReadBits(base, 32))
            return false;
        *id = GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base));
    }
    uint64_t owner{}, retired{};
    if (!r.ReadBits(owner, 32) || !r.ReadBits(Epoch, 64) || !r.ReadBits(Sequence, 64) || !r.ReadBits(Tick, 64) || !r.ReadBits(retired, 1) || !Rig.Deserialize(r))
        return false;
    Owner = static_cast<uint32_t>(owner);
    Retired = retired != 0;
    Decoded = true;
    return Decoded = IsValid();
}
