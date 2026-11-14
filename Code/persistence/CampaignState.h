#pragma once
#include "CampaignData.h"
#include "WorldStore.h"
#include <optional>

namespace Campaign
{
class State
{
public:
    bool Open(const std::filesystem::path& acPath) noexcept;
    bool Bind(const TiltedPhoques::String& acRuntime, const Mods& acContent) noexcept;
    bool LoadObject(GameId aId, ObjectData& aObject, bool& aFound) noexcept;
    bool SaveObject(const ObjectData& acObject) noexcept;
    bool SaveObjects(const TiltedPhoques::Vector<ObjectData>& acObjects) noexcept;
    bool ValidForm(GameId aId, bool aOptional = false) const noexcept;
    bool ValidObject(const ObjectData& acObject) const noexcept;
    const std::optional<Contract>& GetContract() const noexcept { return m_contract; }
    const TiltedPhoques::String& WorldId() const noexcept { return m_store.WorldId(); }
    uint64_t Revision() const noexcept { return m_store.Revision(); }
    const TiltedPhoques::String& Error() const noexcept { return m_error; }
    bool IsReady() const noexcept { return m_ready; }

private:
    TiltedPhoques::String Key(const char* apKind, GameId aId) const noexcept;
    WorldStore m_store;
    std::optional<Contract> m_contract;
    TiltedPhoques::String m_error;
    bool m_ready{};
};

// Checked arithmetic for authoritative snapshots; transfer acknowledgement is a separate layer.
bool ApplyInventoryDelta(Inventory& aInventory, const Inventory::Entry& acDelta) noexcept;
} // namespace Campaign
