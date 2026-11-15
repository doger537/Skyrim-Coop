#pragma once
#include "CampaignData.h"
#include "WorldStore.h"
#include "QuestAdapters.h"
#include <optional>

namespace Campaign
{
class State
{
public:
    struct SavedQuest
    {
        Quest Data;
        uint64_t Revision{};
    };
    enum class QuestResult
    {
        Applied,
        Duplicate,
        Ignored,
        Conflict,
        Invalid,
        StorageError
    };
    bool Open(const std::filesystem::path& acPath, const TiltedPhoques::String& acAdapters = QuestAdapters::kEmpty) noexcept;
    bool Bind(const TiltedPhoques::String& acRuntime, const Mods& acContent) noexcept;
    bool LoadObject(GameId aId, ObjectData& aObject, bool& aFound) noexcept;
    bool SaveObject(const ObjectData& acObject) noexcept;
    bool SaveObjects(const TiltedPhoques::Vector<ObjectData>& acObjects) noexcept;
    bool ValidForm(GameId aId, bool aOptional = false) const noexcept;
    bool ValidObject(const ObjectData& acObject) const noexcept;
    bool BootstrapQuests(const TiltedPhoques::Vector<Quest>& acQuests, bool aAllowMisc) noexcept;
    QuestResult UpdateQuest(GameId aId, uint32_t aEpoch, uint8_t aStatus, uint16_t aStage, uint8_t aType, bool aAllowMisc) noexcept;
    const TiltedPhoques::Vector<SavedQuest>& Quests() const noexcept { return m_quests; }
    const SavedQuest* FindQuest(GameId aId) const noexcept;
    const QuestAdapter* Adapter(GameId aId) const noexcept;
    TiltedPhoques::Vector<GameId> MiscQuestIds() const noexcept;
    uint64_t QuestRevision() const noexcept { return m_questRevision; }
    bool IsBootstrapped() const noexcept { return m_bootstrapped; }
    const std::optional<Contract>& GetContract() const noexcept { return m_contract; }
    const TiltedPhoques::String& WorldId() const noexcept { return m_store.WorldId(); }
    uint64_t Revision() const noexcept { return m_store.Revision(); }
    const TiltedPhoques::String& Error() const noexcept { return m_error; }
    bool IsReady() const noexcept { return m_ready; }

private:
    TiltedPhoques::String Key(const char* apKind, GameId aId) const noexcept;
    bool LoadQuestState(const TiltedPhoques::String& acAdapters) noexcept;
    WorldStore m_store;
    std::optional<Contract> m_contract;
    TiltedPhoques::String m_error;
    bool m_ready{};
    QuestAdapters m_adapters;
    TiltedPhoques::Vector<SavedQuest> m_quests;
    uint64_t m_questRevision{};
    bool m_bootstrapped{};
};

// Checked arithmetic for authoritative snapshots; transfer acknowledgement is a separate layer.
bool ApplyInventoryDelta(Inventory& aInventory, const Inventory::Entry& acDelta) noexcept;
} // namespace Campaign
