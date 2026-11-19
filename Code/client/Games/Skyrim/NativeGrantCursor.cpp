#include <TiltedOnlinePCH.h>
#include "NativeGrantCursor.h"
#include <common/QuestGrantCursorIds.h>
#include <Forms/TESGlobal.h>
#include <Games/TES.h>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <algorithm>

namespace NativeGrantCursor
{
namespace
{
std::atomic<bool> s_writing{};
String Name(uint32_t aPart) noexcept
{
    if (aPart < QuestGrantCursorIds::kWorldWords)
        return String("TrueCoopGrantWorld") + std::to_string(aPart).c_str();
    if (aPart < QuestGrantCursorIds::kVersionWord)
        return String("TrueCoopGrantRevision") + std::to_string(aPart - QuestGrantCursorIds::kWorldWords).c_str();
    return "TrueCoopGrantVersion";
}
TESGlobal* Global(uint32_t aPart) noexcept
{
    const auto* mod = ModManager::Get()->GetByName(QuestGrantCursorIds::kPlugin);
    if (!mod || !mod->IsLoaded() || mod->IsLite() || aPart >= QuestGrantCursorIds::kCount) return nullptr;
    auto* form = TESForm::GetById((uint32_t(mod->standardId) << 24) | (QuestGrantCursorIds::kFirstLocalId + aPart));
    if (!form || static_cast<uint8_t>(form->formType) != 9 || (form->flags & 64)) return nullptr;
    auto* global = static_cast<TESGlobal*>(form);
    return global->unk1C == 'f' && global->unk14.AsAscii() && Name(aPart) == global->unk14.AsAscii() ? global : nullptr;
}
bool Parse(const String& acWorld, std::array<uint16_t, QuestGrantCursorIds::kWorldWords>& aWords) noexcept
{
    if (acWorld.size() != 32) return false;
    for (size_t i = 0; i < acWorld.size(); ++i)
    {
        const char digit = acWorld[i];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) return false;
        aWords[i / 4] = static_cast<uint16_t>((aWords[i / 4] << 4) | (digit <= '9' ? digit - '0' : digit - 'a' + 10));
    }
    return true;
}
struct Writing
{
    Writing() noexcept { s_writing.store(true, std::memory_order_release); }
    ~Writing() noexcept { s_writing.store(false, std::memory_order_release); }
};
}
Result Read(String& aWorld, uint64_t& aRevision) noexcept
{
    aWorld.clear(); aRevision = 0;
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return Result::Invalid;
    std::array<uint16_t, QuestGrantCursorIds::kCount> words{};
    for (uint32_t i = 0; i < words.size(); ++i)
    {
        auto* global = Global(i);
        if (!global || !std::isfinite(global->f) || global->f < 0 || global->f > 65535.f || std::floor(global->f) != global->f) return Result::Invalid;
        words[i] = static_cast<uint16_t>(global->f);
    }
    if (std::all_of(words.begin(), words.end(), [](uint16_t value) { return value == 0; })) return Result::Unbound;
    if (words[QuestGrantCursorIds::kVersionWord] != QuestGrantCursorIds::kVersion) return Result::Invalid;
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < QuestGrantCursorIds::kWorldWords; ++i)
        for (int shift = 12; shift >= 0; shift -= 4) aWorld += hex[(words[i] >> shift) & 15];
    for (size_t i = 0; i < QuestGrantCursorIds::kCursorWords; ++i)
        aRevision |= uint64_t(words[QuestGrantCursorIds::kWorldWords + i]) << (i * 16);
    return aRevision <= uint64_t(std::numeric_limits<int64_t>::max()) ? Result::Bound : Result::Invalid;
}
bool Bind(const String& acWorld) noexcept
{
    String current;
    uint64_t revision{};
    const auto state = Read(current, revision);
    if (state == Result::Bound) return current == acWorld;
    if (state != Result::Unbound) return false;
    std::array<uint16_t, QuestGrantCursorIds::kWorldWords> world{};
    if (!Parse(acWorld, world)) return false;
    const Writing writing;
    for (uint32_t i = 0; i < world.size(); ++i)
    {
        auto* pWord = Global(i);
        if (!pWord || !pWord->SetSavedWord(world[i]))
            return false;
    }
    auto* pVersion = Global(QuestGrantCursorIds::kVersionWord);
    if (!pVersion || !pVersion->SetSavedWord(QuestGrantCursorIds::kVersion))
        return false;
    return Read(current, revision) == Result::Bound && current == acWorld && !revision;
}
bool Advance(const String& acWorld, uint64_t aExpected, uint64_t aNext) noexcept
{
    String current;
    uint64_t revision{};
    if (aNext < aExpected || aNext > uint64_t(std::numeric_limits<int64_t>::max()) || Read(current, revision) != Result::Bound ||
        current != acWorld || revision != aExpected) return false;
    if (aNext == aExpected) return true;
    const Writing writing;
    for (uint32_t i = 0; i < QuestGrantCursorIds::kCursorWords; ++i)
    {
        auto* pWord = Global(QuestGrantCursorIds::kWorldWords + i);
        if (!pWord || !pWord->SetSavedWord(static_cast<uint16_t>(aNext >> (i * 16))))
            return false;
    }
    return Read(current, revision) == Result::Bound && current == acWorld && revision == aNext;
}
bool IsWriting() noexcept { return s_writing.load(std::memory_order_acquire); }
}
