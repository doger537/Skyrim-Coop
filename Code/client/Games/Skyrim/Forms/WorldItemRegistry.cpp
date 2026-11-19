#include <TiltedOnlinePCH.h>
#include "WorldItemRegistry.h"
#include <common/WorldItemRegistryIds.h>
#include <Forms/TESQuest.h>
#include <Forms/TESGlobal.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <Services/PapyrusService.h>
#include <NativeGrantCursor.h>
#include <AI/NativeActorPose.h>
#include <cmath>
#include <atomic>
#include <limits>

namespace WorldItemRegistry
{
namespace
{
std::atomic<bool> s_writing{};
std::atomic<bool> s_tainted{};
struct WriteScope
{
    bool Committed{};
    WriteScope() { s_tainted.store(true, std::memory_order_release); s_writing.store(true, std::memory_order_release); }
    ~WriteScope() { if (Committed) s_tainted.store(false, std::memory_order_release); s_writing.store(false, std::memory_order_release); }
};
struct Alias { void* Vtable; void* Name; TESQuest* Owner; uint32_t Id, Flags; };
static_assert(offsetof(Alias, Owner) == 0x10 && offsetof(Alias, Id) == 0x18 && offsetof(Alias, Flags) == 0x1C);
TESForm* Form(uint32_t aLocal) noexcept
{
    if (!NativeActorPose::OnRunner() || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return nullptr;
    const auto* mod = ModManager::Get()->GetByName(WorldItemRegistryIds::kPlugin);
    return mod && mod->IsLoaded() && !mod->IsLite() ? TESForm::GetById((uint32_t(mod->standardId) << 24) | aLocal) : nullptr;
}
TESQuest* Quest() noexcept
{
    auto* quest = Cast<TESQuest>(Form(WorldItemRegistryIds::kQuestLocalId));
    return quest && quest->stages.Empty() && quest->aliases.length == WorldItemRegistryIds::kSlots && quest->idName.AsAscii() &&
        String(quest->idName.AsAscii()) == WorldItemRegistryIds::kQuestEditorId ? quest : nullptr;
}
Alias* GetAlias(TESQuest* apQuest, uint32_t aSlot) noexcept
{
    if (!apQuest || aSlot >= WorldItemRegistryIds::kSlots) return nullptr;
    for (void* entry : apQuest->aliases)
    {
        auto* alias = static_cast<Alias*>(entry);
        if (alias && alias->Owner == apQuest && alias->Id == aSlot && alias->Flags == 0x29A) return alias;
    }
    return nullptr;
}
TESGlobal* Global(uint32_t aLocal, const String& acName) noexcept
{
    auto* form = Form(aLocal);
    if (!form || static_cast<uint8_t>(form->formType) != 9 || (form->flags & 64)) return nullptr;
    auto* global = static_cast<TESGlobal*>(form);
    return global->unk1C == 'f' && global->unk14.AsAscii() && acName == global->unk14.AsAscii() &&
        std::isfinite(global->f) && global->f >= 0.f && global->f <= 65535.f && std::floor(global->f) == global->f ? global : nullptr;
}
TESGlobal* Word(uint32_t aSlot, uint32_t aWord) noexcept
{
    if (aSlot >= WorldItemRegistryIds::kSlots || aWord >= WorldItemRegistryIds::kWords) return nullptr;
    return Global(WorldItemRegistryIds::kFirstIdLocalId + aSlot * WorldItemRegistryIds::kWords + aWord,
        String("TrueCoopWorldItem") + std::to_string(aSlot).c_str() + "Word" + std::to_string(aWord).c_str());
}
TESGlobal* ReceiptWord(uint32_t aWord) noexcept
{
    return aWord < WorldItemRegistryIds::kWords ? Global(WorldItemRegistryIds::kReceiptFirstLocalId + aWord,
        String("TrueCoopItemReceiptWord") + std::to_string(aWord).c_str()) : nullptr;
}
bool Set(TESGlobal* apGlobal, float aValue) noexcept
{
    const auto* function = World::Get().ctx().at<PapyrusService>().Get("GlobalVariable", "SetValue");
    if (!function || !apGlobal) return false;
    const PapyrusFunction<void, TESGlobal, float> set{function};
    set(apGlobal, aValue);
    return apGlobal->f == aValue;
}
bool Id(uint32_t aSlot, uint64_t& aId) noexcept
{
    aId = 0;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kWords; ++i)
    {
        const auto* word = Word(aSlot, i);
        if (!word) return false;
        aId |= uint64_t(static_cast<uint16_t>(word->f)) << (16 * i);
    }
    return aId <= uint64_t(std::numeric_limits<int64_t>::max());
}
}
bool ReadSlot(uint32_t aSlot, Mapping& aMapping) noexcept
{
    aMapping = {};
    auto* quest = Quest();
    if (!GetAlias(quest, aSlot) || !Id(aSlot, aMapping.Id)) return false;
    auto* reference = quest->GetAliasedRef(aSlot);
    if (!aMapping.Id) return !reference;
    if (!reference || reference->IsDeleted() || !reference->IsTemporary() || !reference->baseForm) return false;
    aMapping.Handle = reference->GetHandle().handle.iBits; aMapping.FormId = reference->formID;
    return aMapping.Handle != 0;
}
bool ContainsReference(TESObjectREFR* apReference) noexcept
{
    auto* quest = Quest();
    if (!quest || !apReference || !apReference->IsTemporary()) return false;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kSlots; ++i)
        if (quest->GetAliasedRef(i) == apReference) return true;
    return false;
}
bool Bind(uint32_t aSlot, uint64_t aId, TESObjectREFR* apReference) noexcept
{
    Mapping before;
    if (!NativeActorPose::OnRunner() || HasPending() || !aId || aId > uint64_t(std::numeric_limits<int64_t>::max()) || !apReference || !apReference->IsTemporary() || apReference->IsDeleted() ||
        !ReadSlot(aSlot, before) || (before.Id && (before.Id != aId || before.FormId != apReference->formID))) return false;
    if (before.Id) return true;
    auto* quest = Quest();
    WriteScope writing;
    ScopedQuestOverride override;
    if (quest->IsStopped()) { bool started{}; quest->EnsureQuestStarted(started, false); if (quest->IsStopped()) return false; }
    TP_THIS_FUNCTION(TForceAlias, void, TESQuest, uint32_t, TESObjectREFR*);
    POINTER_SKYRIMSE(TForceAlias, forceAlias, 25052);
    // MCP 1403D4FA0: native fill data, reference lifetime registration, alias
    // metadata and change handling. No private alias handle is patched.
    TiltedPhoques::ThisCall(forceAlias, quest, aSlot, apReference);
    if (quest->GetAliasedRef(aSlot) != apReference) return false;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kWords; ++i)
        if (!Set(Word(aSlot, i), float((aId >> (16 * i)) & 65535))) return false;
    Mapping after;
    writing.Committed = ReadSlot(aSlot, after) && after.Id == aId && after.FormId == apReference->formID;
    return writing.Committed;
}
bool Clear(uint32_t aSlot, uint64_t aExpectedId) noexcept
{
    if (!NativeActorPose::OnRunner()) return false;
    auto* quest = Quest();
    auto* alias = GetAlias(quest, aSlot);
    uint64_t id{};
    const auto* function = World::Get().ctx().at<PapyrusService>().Get("ReferenceAlias", "Clear");
    if (HasPending() || !alias || !function || !aExpectedId || !Id(aSlot, id) || id != aExpectedId) return false;
    WriteScope writing;
    ScopedQuestOverride override;
    const PapyrusFunction<void, Alias> clear{function}; clear(alias);
    if (quest->GetAliasedRef(aSlot)) return false;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kWords; ++i) if (!Set(Word(aSlot, i), 0.f)) return false;
    Mapping after;
    writing.Committed = ReadSlot(aSlot, after) && !after.Id;
    return writing.Committed;
}
bool ReadReceipt(uint64_t& aCursor, bool& aBound) noexcept
{
    aCursor = 0; aBound = false;
    if (!NativeActorPose::OnRunner()) return false;
    String world; uint64_t grantCursor{};
    if (NativeGrantCursor::Read(world, grantCursor) != NativeGrantCursor::Result::Bound) return false;
    auto* version = Global(WorldItemRegistryIds::kReceiptVersionLocalId, "TrueCoopItemReceiptVersion");
    if (!version || (version->f != 0.f && version->f != float(WorldItemRegistryIds::kVersion))) return false;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kWords; ++i)
    {
        const auto* word = ReceiptWord(i); if (!word) return false;
        aCursor |= uint64_t(static_cast<uint16_t>(word->f)) << (16 * i);
    }
    aBound = version->f != 0.f;
    return aCursor <= uint64_t(std::numeric_limits<int64_t>::max()) && (aBound || !aCursor);
}
bool BindReceipt() noexcept
{
    uint64_t cursor{}; bool bound{};
    if (HasPending() || !ReadReceipt(cursor, bound)) return false;
    if (bound) return true;
    WriteScope writing;
    writing.Committed = Set(Global(WorldItemRegistryIds::kReceiptVersionLocalId, "TrueCoopItemReceiptVersion"), float(WorldItemRegistryIds::kVersion));
    return writing.Committed;
}
bool AdvanceReceipt(uint64_t aExpected, uint64_t aNext) noexcept
{
    uint64_t cursor{}; bool bound{};
    if (HasPending() || aNext < aExpected || aNext > uint64_t(std::numeric_limits<int64_t>::max()) || !ReadReceipt(cursor, bound) || !bound || cursor != aExpected) return false;
    if (aNext == aExpected) return true;
    WriteScope writing;
    for (uint32_t i = 0; i < WorldItemRegistryIds::kWords; ++i) if (!Set(ReceiptWord(i), float((aNext >> (16 * i)) & 65535))) return false;
    writing.Committed = ReadReceipt(cursor, bound) && bound && cursor == aNext;
    return writing.Committed;
}
bool IsWriting() noexcept { return s_writing.load(std::memory_order_acquire); }
bool HasPending() noexcept { return IsWriting() || s_tainted.load(std::memory_order_acquire); }
void AfterLoad() noexcept { s_tainted.store(false, std::memory_order_release); }
}
