#include <TiltedOnlinePCH.h>
#include <Forms/TESGlobal.h>
#include <Misc/GameVM.h>

bool TESGlobal::SetSavedWord(uint16_t aValue) noexcept
{
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0" || static_cast<uint8_t>(formType) != 9 || (flags & 64) || unk1C != 'f')
        return false;

    const auto* pVM = GameVM::Get();
    if (!pVM || !pVM->virtualMachine)
        return false;

    // GlobalVariable.SetValue on 1.7.104, verified in identity-setvalue-byte-results.
    // The native save path writes mutable globals separately from per-form changes.
    using TSetValue = void(BSScript::IVirtualMachine*, uint32_t, const TESGlobal*, float);
    POINTER_SKYRIMSE(TSetValue, setValue, 55923);
    setValue.Get()(pVM->virtualMachine, 0, this, static_cast<float>(aValue));
    return f == static_cast<float>(aValue);
}
