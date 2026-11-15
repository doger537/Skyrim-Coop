
#include <Components.h>
#include <es_loader/ESLoader.h>
#include <Structs/ContentCompatibility.h>

void ModsComponent::BindContract(const Mods& acContract) noexcept
{
    m_standardMods.clear();
    m_liteMods.clear();
    m_seed = 0;
    // Stable IDs are ordinal positions in the pinned manifest, not local load slots.
    for (const auto& entry : acContract.ModList)
        if (entry.IsLite)
            AddLite(entry.Filename);
        else
            AddStandard(entry.Filename);
}

uint32_t ModsComponent::AddStandard(const String& acpFilename) noexcept
{
    const auto name = ContentCompatibility::NormalizeName(acpFilename);
    const auto itor = m_standardMods.find(name);
    if (itor != std::end(m_standardMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    const auto id = m_seed++;
    m_standardMods.emplace(name, Entry{id, 1});

    return id;
}

uint32_t ModsComponent::AddLite(const String& acpFilename) noexcept
{
    const auto name = ContentCompatibility::NormalizeName(acpFilename);
    const auto itor = m_liteMods.find(name);
    if (itor != std::end(m_liteMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    const auto id = m_seed++;
    m_liteMods.emplace(name, Entry{id, 1});

    return id;
}

void ModsComponent::AddServerMod(const ESLoader::PluginData& acData)
{
    // kind of a hack since we want to store both, so we take the two byte value
    m_serverMods.emplace(ContentCompatibility::NormalizeName(acData.m_filename), Entry{acData.m_liteId, 1});
}

bool ModsComponent::IsInstalled(const String& acpFilename) const noexcept
{
    const auto name = ContentCompatibility::NormalizeName(acpFilename);
    auto it = std::find_if(m_serverMods.begin(), m_serverMods.end(), [&](const TModList::value_type& aEntry) { return aEntry.first == name; });

    return it != m_serverMods.end();
}
