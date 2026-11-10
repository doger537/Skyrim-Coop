#include <TiltedOnlinePCH.h>
#include <ModCompat/AlternateStart.h>
#include <Structs/AlternateStartPolicy.h>
#include <Structs/ContentCompatibility.h>
#include <Games/References.h>
#include <Games/TES.h>

bool AlternateStart::IsPersonalQuest(uint32_t aFormId) noexcept
{
    for (const auto* pMod : ModManager::Get()->mods)
    {
        if (pMod->IsLoaded() && ContentCompatibility::NormalizeName(pMod->filename) == AlternateStartPolicy::cPlugin &&
            AlternateStartPolicy::OwnsForm(aFormId, pMod->GetId(), pMod->IsLite()))
            return true;
    }
    return false;
}
