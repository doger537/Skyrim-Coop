#include "CorePluginUpgrade.h"
#include <Structs/ContentCompatibility.h>

bool CorePluginUpgrade::Upgrade(Mods& aPinnedContent) noexcept
{
    for (auto& entry : aPinnedContent.ModList)
        if (ContentCompatibility::NormalizeName(entry.Filename) == "skyrimtogether.esp" && !entry.IsLite && entry.Digest == kPrevious &&
            entry.Masters.size() == 2 && ContentCompatibility::NormalizeName(entry.Masters[0]) == "skyrim.esm" &&
            ContentCompatibility::NormalizeName(entry.Masters[1]) == "update.esm")
        {
            entry.Digest = kMarkers;
            return true;
        }
    return false;
}
