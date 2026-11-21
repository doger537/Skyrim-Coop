#include <TiltedOnlinePCH.h>
#include "SavedCharacterIdentity.h"
#include <Forms/TESGlobal.h>
#include <Games/TES.h>
#include <common/CharacterIdentityIds.h>

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
    for (uint32_t part = 0; part < acIdentity.Parts.size(); ++part)
    {
        auto* pWord = Global(part);
        if (!pWord || !pWord->SetSavedWord(acIdentity.Parts[part]))
            return false;
    }
    auto* pVersion = Global(8);
    if (!pVersion || !pVersion->SetSavedWord(CharacterIdentityIds::kVersion))
        return false;
    return Read(current) == Result::Found && current == acIdentity;
}
}
