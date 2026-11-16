#include "WorldItemState.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace
{
using TiltedPhoques::Buffer;

bool Read(Buffer::Reader& aReader, uint64_t& aValue, size_t aBits = 64) noexcept
{
    return aReader.ReadBits(aValue, aBits);
}

bool ReadFloat(Buffer::Reader& aReader, float& aValue) noexcept
{
    uint64_t bits = 0;
    if (!Read(aReader, bits, 32))
        return false;
    aValue = std::bit_cast<float>(static_cast<uint32_t>(bits));
    return std::isfinite(aValue);
}

void WriteFloat(Buffer::Writer& aWriter, float aValue) noexcept
{
    aWriter.WriteBits(std::bit_cast<uint32_t>(aValue), 32);
}

void WriteId(Buffer::Writer& aWriter, GameId aId) noexcept
{
    aWriter.WriteBits(aId.ModId, 32);
    aWriter.WriteBits(aId.BaseId, 32);
}

bool ReadId(Buffer::Reader& aReader, GameId& aId) noexcept
{
    uint64_t mod = 0, base = 0;
    if (!Read(aReader, mod, 32) || !Read(aReader, base, 32))
        return false;
    aId = GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base));
    return true;
}

void WriteItem(Buffer::Writer& aWriter, const Inventory::Entry& acItem) noexcept
{
    WriteId(aWriter, acItem.BaseId);
    aWriter.WriteBits(static_cast<uint32_t>(acItem.Count), 32);
    WriteFloat(aWriter, acItem.ExtraCharge);
    WriteId(aWriter, acItem.ExtraEnchantId);
    aWriter.WriteBits(acItem.ExtraEnchantCharge, 16);
    aWriter.WriteBits(acItem.EnchantData.Effects.size(), 8);
    for (const auto& effect : acItem.EnchantData.Effects)
    {
        WriteFloat(aWriter, effect.Magnitude);
        aWriter.WriteBits(static_cast<uint32_t>(effect.Area), 32);
        aWriter.WriteBits(static_cast<uint32_t>(effect.Duration), 32);
        WriteFloat(aWriter, effect.RawCost);
        WriteId(aWriter, effect.EffectId);
    }
    WriteFloat(aWriter, acItem.ExtraHealth);
    WriteId(aWriter, acItem.ExtraPoisonId);
    aWriter.WriteBits(acItem.ExtraPoisonCount, 32);
    aWriter.WriteBits(static_cast<uint32_t>(acItem.ExtraSoulLevel), 32);
    aWriter.WriteBits(acItem.EnchantData.IsWeapon, 1);
    aWriter.WriteBits(acItem.ExtraEnchantRemoveUnequip, 1);
    aWriter.WriteBits(acItem.ExtraWorn, 1);
    aWriter.WriteBits(acItem.ExtraWornLeft, 1);
    aWriter.WriteBits(acItem.IsQuestItem, 1);
}

// Deliberately bounded and checked rather than Entry::Deserialize's legacy
// unbounded effect count. A failed read never reaches world application.
bool ReadItem(Buffer::Reader& aReader, Inventory::Entry& aItem) noexcept
{
    uint64_t bits = 0;
    if (!ReadId(aReader, aItem.BaseId) || !Read(aReader, bits, 32))
        return false;
    aItem.Count = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
    if (!ReadFloat(aReader, aItem.ExtraCharge) || !ReadId(aReader, aItem.ExtraEnchantId) || !Read(aReader, bits, 16))
        return false;
    aItem.ExtraEnchantCharge = static_cast<uint16_t>(bits);
    if (!Read(aReader, bits, 8) || bits > WorldItemState::kMaxEffects)
        return false;
    aItem.EnchantData.Effects.resize(static_cast<size_t>(bits));
    for (auto& effect : aItem.EnchantData.Effects)
    {
        if (!ReadFloat(aReader, effect.Magnitude) || !Read(aReader, bits, 32))
            return false;
        effect.Area = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
        if (!Read(aReader, bits, 32))
            return false;
        effect.Duration = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
        if (!ReadFloat(aReader, effect.RawCost) || !ReadId(aReader, effect.EffectId))
            return false;
    }
    if (!ReadFloat(aReader, aItem.ExtraHealth) || !ReadId(aReader, aItem.ExtraPoisonId) || !Read(aReader, bits, 32))
        return false;
    aItem.ExtraPoisonCount = static_cast<uint32_t>(bits);
    if (!Read(aReader, bits, 32))
        return false;
    aItem.ExtraSoulLevel = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
    bool* flags[] = {&aItem.EnchantData.IsWeapon, &aItem.ExtraEnchantRemoveUnequip, &aItem.ExtraWorn, &aItem.ExtraWornLeft, &aItem.IsQuestItem};
    for (bool* pFlag : flags)
    {
        if (!Read(aReader, bits, 1))
            return false;
        *pFlag = bits != 0;
    }
    return true;
}

void WriteMotion(Buffer::Writer& aWriter, const WorldItemMotion& acMotion) noexcept
{
    for (uint64_t value : {acMotion.Id, acMotion.WorldEpoch, acMotion.HostEpoch, acMotion.Revision, acMotion.Sequence, acMotion.Tick})
        aWriter.WriteBits(value, 64);
    WriteId(aWriter, acMotion.CellId);
    WriteId(aWriter, acMotion.WorldSpaceId);
    acMotion.Position.Serialize(aWriter);
    for (float value : acMotion.Rotation)
        WriteFloat(aWriter, value);
    for (const auto& vector : {acMotion.LinearVelocity, acMotion.AngularVelocity})
        for (float value : vector)
            WriteFloat(aWriter, value);
    aWriter.WriteBits(static_cast<uint8_t>(acMotion.Mode), 8);
}

bool ReadMotion(Buffer::Reader& aReader, WorldItemMotion& aMotion) noexcept
{
    uint64_t* fields[] = {&aMotion.Id, &aMotion.WorldEpoch, &aMotion.HostEpoch, &aMotion.Revision, &aMotion.Sequence, &aMotion.Tick};
    for (uint64_t* pField : fields)
        if (!Read(aReader, *pField))
            return false;
    uint64_t bits = 0;
    if (!ReadId(aReader, aMotion.CellId) || !ReadId(aReader, aMotion.WorldSpaceId) || !Read(aReader, bits))
        return false;
    aMotion.Position.Unpack(bits);
    for (float& value : aMotion.Rotation)
        if (!ReadFloat(aReader, value))
            return false;
    for (auto* pVector : {&aMotion.LinearVelocity, &aMotion.AngularVelocity})
        for (float& value : *pVector)
            if (!ReadFloat(aReader, value))
                return false;
    if (!Read(aReader, bits, 8))
        return false;
    aMotion.Mode = static_cast<WorldItemMotion::Motion>(bits);
    return true;
}
}

bool WorldItemMotion::IsValid() const noexcept
{
    if (!Decoded || Id == 0 || WorldEpoch == 0 || HostEpoch == 0 || Revision == 0 || Sequence == 0 ||
        !CellId || Mode > Motion::Kinematic)
        return false;
    for (float coordinate : {Position.x, Position.y, Position.z})
        if (!std::isfinite(coordinate) || std::abs(coordinate) > 1048575.f)
            return false;
    float norm = 0.f;
    for (float value : Rotation)
    {
        if (!std::isfinite(value))
            return false;
        norm += value * value;
    }
    if (!std::isfinite(norm) || std::abs(norm - 1.f) > 0.01f)
        return false;
    for (const auto& vector : {LinearVelocity, AngularVelocity})
        for (float value : vector)
            if (!std::isfinite(value) || std::abs(value) > 65536.f)
                return false;
    return true;
}

bool WorldItemState::IsValid() const noexcept
{
    return WorldItemMotion::IsValid() && Item.BaseId && Item.Count > 0 && Item.EnchantData.Effects.size() <= kMaxEffects &&
           std::isfinite(Item.ExtraCharge) && std::isfinite(Item.ExtraHealth) &&
           std::all_of(Item.EnchantData.Effects.begin(), Item.EnchantData.Effects.end(), [](const auto& effect)
           { return effect.EffectId && std::isfinite(effect.Magnitude) && std::isfinite(effect.RawCost); });
}

void WorldItemMotion::Serialize(Buffer::Writer& aWriter) const noexcept
{
    const bool valid = IsValid();
    aWriter.WriteBits(valid, 1);
    if (!valid)
        return;
    WriteMotion(aWriter, *this);
}

void WorldItemMotion::Deserialize(Buffer::Reader& aReader) noexcept
{
    *this = {};
    Decoded = false;
    uint64_t valid = 0;
    if (!Read(aReader, valid, 1) || valid == 0 || !ReadMotion(aReader, *this))
        return;
    Decoded = true;
    Decoded = IsValid();
}

void WorldItemState::Serialize(Buffer::Writer& aWriter) const noexcept
{
    const bool valid = IsValid();
    aWriter.WriteBits(valid, 1);
    if (!valid)
        return;
    WriteMotion(aWriter, *this);
    WriteItem(aWriter, Item);
    aWriter.WriteBits(Deleted, 1);
}

void WorldItemState::Deserialize(Buffer::Reader& aReader) noexcept
{
    *this = {};
    Decoded = false;
    uint64_t bits = 0;
    if (!Read(aReader, bits, 1) || bits == 0)
        return;
    if (!ReadMotion(aReader, *this) || !ReadItem(aReader, Item) || !Read(aReader, bits, 1))
        return;
    Deleted = bits != 0;
    Decoded = true;
    Decoded = IsValid();
}
