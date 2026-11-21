#include "CorePluginUpgrade.h"
#include <Structs/ContentCompatibility.h>

bool CorePluginUpgrade::Upgrade(Mods& aPinnedContent) noexcept
{
    bool changed{};
    for (auto& entry : aPinnedContent.ModList)
        if (ContentCompatibility::NormalizeName(entry.Filename) == "skyrimtogether.esp" && !entry.IsLite && (entry.Digest == kPrevious || entry.Digest == kMarkers || entry.Digest == kCharacters) &&
            entry.Masters.size() == 2 && ContentCompatibility::NormalizeName(entry.Masters[0]) == "skyrim.esm" &&
            ContentCompatibility::NormalizeName(entry.Masters[1]) == "update.esm")
        {
            entry.Digest = kCheckpoints;
            changed = true;
        }
        else if (ContentCompatibility::NormalizeName(entry.Filename) == "truecoopworldstate.esp" && !entry.IsLite &&
            entry.Digest == kGrantCursor && entry.Masters.size() == 1 && ContentCompatibility::NormalizeName(entry.Masters[0]) == "skyrim.esm")
        {
            // Preserve the exact thirteen existing globals; append only our
            // script-free registry and saved ID/receipt globals. No overrides.
            entry.Digest = kItemRegistry;
            changed = true;
        }
    return changed;
}
