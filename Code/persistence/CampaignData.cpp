#include "CampaignData.h"
#include "WorldStore.h"
#include <Structs/ContentCompatibility.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <type_traits>

namespace Campaign
{
namespace
{
using TiltedPhoques::String;
using TiltedPhoques::Vector;

class Writer
{
public:
    explicit Writer(Vector<uint8_t>& aBytes) noexcept
        : m_bytes(aBytes)
    {
        m_bytes.clear();
    }
    template <class T> bool Number(T aValue) noexcept
    {
        using U = std::make_unsigned_t<T>;
        const U value = static_cast<U>(aValue);
        for (size_t i = 0; i < sizeof(T); ++i)
            m_bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
        return m_bytes.size() <= WorldStore::kMaxRecordBytes;
    }
    bool Number(bool aValue) noexcept { return Number(static_cast<uint8_t>(aValue)); }
    bool Number(float aValue) noexcept { return std::isfinite(aValue) && Number(std::bit_cast<uint32_t>(aValue)); }
    bool Raw(const uint8_t* apData, size_t aSize) noexcept
    {
        if (m_bytes.size() > WorldStore::kMaxRecordBytes || aSize > WorldStore::kMaxRecordBytes - m_bytes.size())
            return false;
        m_bytes.insert(m_bytes.end(), apData, apData + aSize);
        return true;
    }
    bool Text(const String& acText, size_t aLimit) noexcept
    {
        return acText.size() <= aLimit && Number(static_cast<uint16_t>(acText.size())) && Raw(reinterpret_cast<const uint8_t*>(acText.data()), acText.size());
    }
    template <class T, class F> bool List(const Vector<T>& acList, size_t aLimit, F aFields) noexcept
    {
        if (acList.size() > aLimit || !Number(static_cast<uint32_t>(acList.size())))
            return false;
        for (const auto& entry : acList)
            if (!aFields(*this, entry))
                return false;
        return true;
    }

private:
    Vector<uint8_t>& m_bytes;
};

class Reader
{
public:
    explicit Reader(const Vector<uint8_t>& acBytes) noexcept
        : m_bytes(acBytes)
    {
    }
    template <class T> bool Number(T& aValue) noexcept
    {
        if (sizeof(T) > m_bytes.size() - m_position)
            return false;
        using U = std::make_unsigned_t<T>;
        U value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= U(m_bytes[m_position++]) << (8 * i);
        aValue = std::bit_cast<T>(value);
        return true;
    }
    bool Number(bool& aValue) noexcept
    {
        uint8_t value = 0;
        if (!Number(value) || value > 1)
            return false;
        aValue = value != 0;
        return true;
    }
    bool Number(float& aValue) noexcept
    {
        uint32_t value = 0;
        if (!Number(value))
            return false;
        aValue = std::bit_cast<float>(value);
        return std::isfinite(aValue);
    }
    bool Raw(uint8_t* apData, size_t aSize) noexcept
    {
        if (aSize > m_bytes.size() - m_position)
            return false;
        std::copy_n(m_bytes.data() + m_position, aSize, apData);
        m_position += aSize;
        return true;
    }
    bool Text(String& aText, size_t aLimit) noexcept
    {
        uint16_t size = 0;
        if (!Number(size) || size > aLimit || size > m_bytes.size() - m_position)
            return false;
        aText.assign(reinterpret_cast<const char*>(m_bytes.data() + m_position), size);
        m_position += size;
        return true;
    }
    template <class T, class F> bool List(Vector<T>& aList, size_t aLimit, F aFields) noexcept
    {
        uint32_t count = 0;
        if (!Number(count) || count > aLimit || count > m_bytes.size() - m_position)
            return false;
        aList.clear();
        aList.resize(count);
        for (auto& entry : aList)
            if (!aFields(*this, entry))
                return false;
        return true;
    }
    bool End() const noexcept { return m_position == m_bytes.size(); }

private:
    const Vector<uint8_t>& m_bytes;
    size_t m_position{};
};

template <class A, class T> bool Form(A& aArchive, T& aId) noexcept
{
    return aArchive.Number(aId.ModId) && aArchive.Number(aId.BaseId);
}

template <class A, class T> bool Effect(A& aArchive, T& aEffect) noexcept
{
    return aArchive.Number(aEffect.Magnitude) && aArchive.Number(aEffect.Area) && aArchive.Number(aEffect.Duration) && aArchive.Number(aEffect.RawCost) &&
           Form(aArchive, aEffect.EffectId);
}

template <class A, class T> bool Entry(A& aArchive, T& aEntry) noexcept
{
    return Form(aArchive, aEntry.BaseId) && aArchive.Number(aEntry.Count) && aArchive.Number(aEntry.ExtraCharge) && Form(aArchive, aEntry.ExtraEnchantId) &&
           aArchive.Number(aEntry.ExtraEnchantCharge) && aArchive.Number(aEntry.EnchantData.IsWeapon) &&
           aArchive.List(aEntry.EnchantData.Effects, 256, [](auto& archive, auto& effect) { return Effect(archive, effect); }) && aArchive.Number(aEntry.ExtraHealth) &&
           Form(aArchive, aEntry.ExtraPoisonId) && aArchive.Number(aEntry.ExtraPoisonCount) && aArchive.Number(aEntry.ExtraSoulLevel) &&
           aArchive.Number(aEntry.ExtraEnchantRemoveUnequip) && aArchive.Number(aEntry.ExtraWorn) && aArchive.Number(aEntry.ExtraWornLeft) && aArchive.Number(aEntry.IsQuestItem);
}

template <class A, class T> bool Object(A& aArchive, T& aObject) noexcept
{
    return Form(aArchive, aObject.Id) && Form(aArchive, aObject.CellId) && Form(aArchive, aObject.WorldSpaceId) && aArchive.Number(aObject.CurrentCoords.X) &&
           aArchive.Number(aObject.CurrentCoords.Y) && aArchive.Number(aObject.CurrentLockData.IsLocked) && aArchive.Number(aObject.CurrentLockData.LockLevel) &&
           aArchive.List(aObject.CurrentInventory.Entries, 4096, [](auto& archive, auto& entry) { return Entry(archive, entry); }) &&
           Form(aArchive, aObject.CurrentInventory.CurrentMagicEquipment.LeftHandSpell) && Form(aArchive, aObject.CurrentInventory.CurrentMagicEquipment.RightHandSpell) &&
           Form(aArchive, aObject.CurrentInventory.CurrentMagicEquipment.Shout);
}

template <class A, class T> bool Manifest(A& aArchive, T& aContract) noexcept
{
    return aArchive.Text(aContract.Runtime, 32) &&
           aArchive.List(
               aContract.Content.ModList, 4096,
               [](auto& archive, auto& entry)
               {
                   return archive.Text(entry.Filename, 255) && archive.Number(entry.Id) && archive.Number(entry.IsLite) && archive.Raw(entry.Digest.data(), entry.Digest.size()) &&
                          archive.List(entry.Masters, 254, [](auto& masterArchive, auto& name) { return masterArchive.Text(name, 255); });
               }) &&
           aArchive.List(
               aContract.Content.Resources, 4096,
               [](auto& archive, auto& resource) { return archive.Text(resource.Filename, 255) && archive.Raw(resource.Digest.data(), resource.Digest.size()); });
}

template <class A, class T> bool QuestFields(A& aArchive, T& aQuest) noexcept
{
    return Form(aArchive, aQuest.Id) && aArchive.Number(aQuest.Epoch) && aArchive.Number(aQuest.Type) && aArchive.Number(aQuest.Started) && aArchive.Number(aQuest.Stopped) &&
           aArchive.List(aQuest.Events, 65536, [](auto& archive, auto& event) { return archive.Number(event.Status) && archive.Number(event.Stage) && event.Status <= 2; }) &&
           aArchive.List(aQuest.Decisions, 256, [](auto& archive, auto& decision) { return archive.Text(decision.Group, 64) && archive.Number(decision.Stage); });
}

bool Header(Writer& aWriter, uint8_t aKind) noexcept
{
    return aWriter.Number(uint32_t{0x53574354}) && aWriter.Number(uint16_t{1}) && aWriter.Number(aKind);
}

bool Header(Reader& aReader, uint8_t aKind) noexcept
{
    uint32_t magic = 0;
    uint16_t schema = 0;
    uint8_t kind = 0;
    return aReader.Number(magic) && aReader.Number(schema) && aReader.Number(kind) && magic == 0x53574354 && schema == 1 && kind == aKind;
}
} // namespace

bool Encode(const Contract& acContract, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept
{
    Writer writer(aBytes);
    return ContentCompatibility::Validate(acContract.Content) && Header(writer, 1) && Manifest(writer, acContract);
}

bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, Contract& aContract) noexcept
{
    aContract = {};
    Reader reader(acBytes);
    return acBytes.size() <= WorldStore::kMaxRecordBytes && Header(reader, 1) && Manifest(reader, aContract) && reader.End() && ContentCompatibility::Validate(aContract.Content);
}

bool Encode(const ObjectData& acObject, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept
{
    Writer writer(aBytes);
    return Header(writer, 2) && Object(writer, acObject);
}

bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, ObjectData& aObject) noexcept
{
    aObject = {};
    Reader reader(acBytes);
    return acBytes.size() <= WorldStore::kMaxRecordBytes && Header(reader, 2) && Object(reader, aObject) && reader.End();
}

bool Encode(const Quest& acQuest, TiltedPhoques::Vector<uint8_t>& aBytes) noexcept
{
    Writer writer(aBytes);
    return acQuest.Epoch != 0 && Header(writer, 3) && QuestFields(writer, acQuest);
}

bool Decode(const TiltedPhoques::Vector<uint8_t>& acBytes, Quest& aQuest) noexcept
{
    aQuest = {};
    Reader reader(acBytes);
    return acBytes.size() <= WorldStore::kMaxRecordBytes && Header(reader, 3) && QuestFields(reader, aQuest) && aQuest.Epoch != 0 && reader.End();
}
} // namespace Campaign
