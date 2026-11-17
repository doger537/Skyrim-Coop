#include <TiltedOnlinePCH.h>
#include "PlayerMarkerQuest.h"
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <Services/PapyrusService.h>
#include <common/PlayerMarkerIds.h>
#include <cstring>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <ScopedReference.h>
#include <Structs/GameId.h>

namespace PlayerMarkerQuest
{
namespace
{
// MCP: ReferenceAlias ForceRefTo/Clear callbacks read owner +10, alias ID +18
// and optional flag +1C. No alias-internal fill data is read or written here.
struct ReferenceAlias
{
    void* Vtable;
    void* Name;
    TESQuest* Owner;
    uint32_t Id;
    uint32_t Flags;
};
static_assert(offsetof(ReferenceAlias, Owner) == 0x10);
static_assert(offsetof(ReferenceAlias, Id) == 0x18);
static_assert(offsetof(ReferenceAlias, Flags) == 0x1C);

ReferenceAlias* Alias(TESQuest* apQuest, uint32_t aSlot) noexcept
{
    if (!apQuest || aSlot >= PlayerMarkers::kSlotCount || apQuest->aliases.length != PlayerMarkers::kSlotCount)
        return nullptr;
    for (void* entry : apQuest->aliases)
    {
        auto* alias = static_cast<ReferenceAlias*>(entry);
        if (alias && alias->Owner == apQuest && alias->Id == aSlot && (alias->Flags & 2) != 0)
            return alias;
    }
    return nullptr;
}
const void* Function(const char* apNamespace, const char* apName) noexcept
{
    return World::Get().ctx().at<PapyrusService>().Get(apNamespace, apName);
}
void Display(TESQuest* apQuest, uint32_t aSlot, bool aVisible) noexcept
{
    const auto* function = Function("Quest", "SetObjectiveDisplayed");
    if (function)
    {
        const PapyrusFunction<void, TESQuest, uint32_t, bool, bool> callback{function};
        callback(apQuest, aSlot, aVisible, false);
    }
}
}

bool Ready() noexcept
{
    return VersionDb::Get().GetLoadedVersionString() == "1.7.104.0" && Function("Quest", "SetObjectiveDisplayed") && Function("ReferenceAlias", "Clear");
}
TESQuest* Get() noexcept
{
    const auto* mod = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!mod || !mod->IsLoaded() || mod->IsLite())
        return nullptr;
    auto* quest = Cast<TESQuest>(TESForm::GetById((uint32_t(mod->standardId) << 24) | PlayerMarkers::kQuestLocalId));
    // Reject a stale/incompatible ESP instead of interpreting an unrelated form.
    if (!quest || !quest->stages.Empty() || quest->aliases.length != PlayerMarkers::kSlotCount ||
        !quest->idName.AsAscii() || std::strcmp(quest->idName.AsAscii(), PlayerMarkers::kQuestEditorId) != 0)
        return nullptr;
    return quest;
}
bool Bind(TESQuest* apQuest, uint32_t aSlot, TESObjectREFR* apReference) noexcept
{
    if (!Ready() || !apReference || !Alias(apQuest, aSlot))
        return false;
    ScopedQuestOverride override;
    if (apQuest->IsStopped())
    {
        bool started{};
        apQuest->EnsureQuestStarted(started, false);
        if (apQuest->IsStopped())
            return false;
    }
    // MCP ID25052 / 1403D4FA0, (quest, uint32 alias, reference), void.
    TP_THIS_FUNCTION(TForceAlias, void, TESQuest, uint32_t, TESObjectREFR*);
    POINTER_SKYRIMSE(TForceAlias, forceAlias, 25052);
    TiltedPhoques::ThisCall(forceAlias, apQuest, aSlot, apReference);
    if (apQuest->GetAliasedRef(aSlot) != apReference)
        return false;
    Display(apQuest, aSlot, true);
    apQuest->SetActive(true);
    return true;
}
void Clear(TESQuest* apQuest, uint32_t aSlot) noexcept
{
    auto* alias = Alias(apQuest, aSlot);
    const auto* function = Function("ReferenceAlias", "Clear");
    if (!Ready() || !alias || !function)
        return;
    ScopedQuestOverride override;
    Display(apQuest, aSlot, false);
    const PapyrusFunction<void, ReferenceAlias> callback{function};
    callback(alias);
}
bool IsAnchor(const TESObjectREFR* apReference) noexcept
{
    const auto* mod = ModManager::Get()->GetByName("SkyrimTogether.esp");
    return apReference && mod && mod->IsLoaded() && apReference->baseForm &&
           apReference->baseForm->formID == ((uint32_t(mod->standardId) << 24) | PlayerMarkers::kAnchorLocalId);
}
uint32_t CreateAnchor(const GameId& acCell, const GameId& acWorld, const NiPoint3& acPosition) noexcept
{
    const auto* mod = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!Ready() || !mod || !mod->IsLoaded() || mod->IsLite())
        return 0;
    auto* base = TESForm::GetById((uint32_t(mod->standardId) << 24) | PlayerMarkers::kAnchorLocalId);
    auto& mods = World::Get().GetModSystem();
    auto* cell = Cast<TESObjectCELL>(TESForm::GetById(mods.GetGameId(acCell)));
    auto* world = acWorld ? Cast<TESWorldSpace>(TESForm::GetById(mods.GetGameId(acWorld))) : nullptr;
    if (!base || (!world && !cell) || (acWorld && !world))
        return 0;
    NiPoint3 position = acPosition;
    NiPoint3 rotation{};
    // MCP: the factory's exterior cell lookup is cache-only (1401B4B50),
    // with a world-persistent-cell fallback (1403106F0). No LoadCell call.
    return ModManager::Get()->SpawnObject(base, position, rotation, cell, world);
}
bool MoveAnchor(uint32_t aHandle, const NiPoint3& acPosition) noexcept
{
    ScopedReference reference(aHandle);
    if (!reference || !IsAnchor(reference.Get()) || reference.Get()->IsDeleted())
        return false;
    TP_THIS_FUNCTION(TSetPosition, void, TESObjectREFR, const NiPoint3&);
    POINTER_SKYRIMSE(TSetPosition, setPosition, 19790);
    TiltedPhoques::ThisCall(setPosition, reference.Get(), acPosition);
    return true;
}
void DestroyAnchor(uint32_t aHandle) noexcept
{
    ScopedReference reference(aHandle);
    if (reference && IsAnchor(reference.Get()) && reference.Get()->IsTemporary() && !reference.Get()->IsDeleted())
        reference.Get()->Delete();
}
}
