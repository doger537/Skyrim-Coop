#include <TiltedOnlinePCH.h>
#include "SavedCharacterIdentity.h"
#include <Forms/TESGlobal.h>
#include <Games/TES.h>
#include <Services/PapyrusService.h>
#include <common/CharacterIdentityIds.h>
#include <cstring>

namespace SavedCharacterIdentity
{
namespace
{
TESGlobal* Global(uint32_t aPart) noexcept
{
    const auto* mod = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!mod || !mod->IsLoaded() || mod->IsLite() || aPart > 8)
        return nullptr;
    auto* form = TESForm::GetById((uint32_t(mod->standardId) << 24) | (CharacterIdentityIds::kFirstPartLocalId + aPart));
    // MCP: TESGlobal loader explicitly checks type 9, FNAM +30, FLTV +34.
    if (!form || static_cast<uint8_t>(form->formType) != 9 || (form->flags & 64) != 0)
        return nullptr;
    auto* global = static_cast<TESGlobal*>(form);
    const auto expected = aPart == 8 ? String("TrueCoopCharacterVersion") : String("TrueCoopCharacterPart") + std::to_string(aPart).c_str();
    if (global->unk1C != 'f' || !global->unk14.AsAscii() || expected != global->unk14.AsAscii())
        return nullptr;
    return global;
}
}
Result Read(CharacterIdentity& aIdentity) noexcept
{
    aIdentity = {};
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0")
        return Result::Invalid;
    auto* version = Global(8);
    if (!version)
        return Result::Invalid;
    std::array<float, 8> values{};
    for (uint32_t part = 0; part < values.size(); ++part)
    {
        auto* global = Global(part);
        if (!global)
            return Result::Invalid;
        values[part] = global->f;
    }
    if (!CharacterIdentity::FromGlobals(values, aIdentity))
        return Result::Invalid;
    if (version->f == 0.f && !aIdentity.IsValid())
        return Result::Unassigned;
    return version->f == float(CharacterIdentityIds::kVersion) && aIdentity.IsValid() ? Result::Found : Result::Invalid;
}
bool Commit(const CharacterIdentity& acIdentity) noexcept
{
    if (!acIdentity.IsValid())
        return false;
    CharacterIdentity current;
    const auto result = Read(current);
    if (result == Result::Found)
        return current == acIdentity;
    if (result != Result::Unassigned)
        return false;
    const auto* function = World::Get().ctx().at<PapyrusService>().Get("GlobalVariable", "SetValue");
    if (!function)
        return false;
    // MCP registration/callback 140A39F10/140A39EC0: void(VM*,stack,TESGlobal*,float).
    // The callback writes +34 without MarkChanged. Native 14061DDA0 saves ALL
    // nonconstant globals by form ID; 14061DE80 restores them. Do not invent a
    // per-form change flag or modify the user's .ess files ourselves.
    const PapyrusFunction<void, TESGlobal, float> setValue{function};
    for (uint32_t part = 0; part < acIdentity.Parts.size(); ++part)
        setValue(Global(part), float(acIdentity.Parts[part]));
    setValue(Global(8), float(CharacterIdentityIds::kVersion)); // Commit marker last.
    return Read(current) == Result::Found && current == acIdentity;
}
}
