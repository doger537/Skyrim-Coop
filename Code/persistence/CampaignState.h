#pragma once
#include "CampaignData.h"
#include "WorldStore.h"
#include "QuestAdapters.h"
#include "QuestRunParticipants.h"
#include "QuestGrant.h"
#include "WorldItemReceipt.h"
#include <Structs/PlacedPhysicsState.h>
#include <Structs/PhysicsFrame.h>
#include <optional>
#include <Structs/CharacterIdentity.h>

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
    // Start a new live timeline. Never roll back this marker with an in-game save.
    bool AdvanceWorldEpoch() noexcept;
    bool RegisterParticipant(const CharacterIdentity& acIdentity) noexcept;
    bool ReserveCheckpoint(uint64_t& aId) noexcept;
    bool CaptureCheckpoint(uint64_t aId, const TiltedPhoques::Vector<uint8_t>& acMetadata) noexcept;
    bool ReadCheckpoint(uint64_t aId, WorldStore::Checkpoint& aCheckpoint) noexcept;
    bool RestoreCheckpoint(uint64_t aId) noexcept;
    bool LastRestoredCheckpoint(uint64_t& aId, uint64_t& aEpoch) noexcept;
    bool IsRestorePending(bool& aPending) noexcept;
    bool CompleteRestore() noexcept;
    uint64_t WorldEpoch() const noexcept { return m_worldEpoch; }
    enum class ItemWriteResult
    {
        Saved,
        Conflict,
        Invalid,
        StorageError
    };
    bool ValidWorldItem(const WorldItemState& acItem) const noexcept;
    bool LoadWorldItems(TiltedPhoques::Vector<WorldItemState>& aItems) noexcept;
    bool LoadPlacedPhysics(TiltedPhoques::Vector<PlacedPhysicsState>& aStates) noexcept;
    bool SavePlacedPhysics(const TiltedPhoques::Vector<PlacedPhysicsState>& acStates) noexcept;
    bool SavePhysicsFrame(const PhysicsFrame& acFrame, const PhysicsFrame::Digest& acDigest) noexcept;
    bool ReadPhysicsFrame(uint64_t aId, const PhysicsFrame::Digest& acDigest, PhysicsFrame& aFrame) noexcept;
    // Id == 0 creates a new persistent identity. Existing IDs require the last
    // committed lifecycle revision, never a motion sample sequence. Inventory
    // quantity/variant changes belong to TransferWorldItem, not motion writes.
    ItemWriteResult SaveWorldItem(WorldItemState& aItem, uint64_t aExpectedRevision) noexcept;
    enum class ItemTransferResult
    {
        Committed,
        Duplicate,
        Conflict,
        Invalid,
        StorageError
    };
    // Delta < 0: create a drop, Id/expected revision must be zero. Delta > 0:
    // claim part/all of an existing live stack at its last lifecycle revision.
    // Commits the lifecycle, immutable receipt, inbox and operation index in
    // one transaction. It does not mutate an actor's transient inventory.
    ItemTransferResult TransferWorldItem(
        WorldItemState& aItem, uint64_t aExpectedRevision, const CharacterIdentity& acRecipient, const CharacterIdentity& acSession, uint64_t aOperation, int32_t aDelta,
        WorldItemReceipt& aReceipt) noexcept;
    bool ReadWorldItemReceipts(const CharacterIdentity& acRecipient, uint64_t aAfterRevision, TiltedPhoques::Vector<WorldItemReceipt>& aReceipts) noexcept;
    bool LoadObject(GameId aId, ObjectData& aObject, bool& aFound) noexcept;
    bool SaveObject(const ObjectData& acObject) noexcept;
    bool SaveObjects(const TiltedPhoques::Vector<ObjectData>& acObjects) noexcept;
    bool ValidForm(GameId aId, bool aOptional = false) const noexcept;
    bool ValidObject(const ObjectData& acObject) const noexcept;
    bool ValidInventory(const Inventory& acInventory) const noexcept;
    bool BootstrapQuests(const TiltedPhoques::Vector<Quest>& acQuests, bool aAllowMisc) noexcept;
    QuestResult
    UpdateQuest(GameId aId, uint32_t aEpoch, uint8_t aStatus, uint16_t aStage, uint8_t aType, bool aAllowMisc, const QuestRunParticipants* apParticipants = nullptr) noexcept;
    QuestResult
    PreviewQuest(GameId aId, uint32_t aEpoch, uint8_t aStatus, uint16_t aStage, uint8_t aType, bool aAllowMisc, const QuestRunParticipants* apParticipants = nullptr) noexcept;
    bool ReadQuestRunParticipants(GameId aId, uint32_t aEpoch, QuestRunParticipants& aParticipants, bool& aFound, bool aShared = false) noexcept;
    bool BindSharedQuestRun(GameId aId, uint32_t aEpoch, const QuestRunParticipants& acParticipants) noexcept;
    enum class GrantResult
    {
        Committed,
        Duplicate,
        Conflict,
        Invalid,
        StorageError
    };
    GrantResult CommitQuestGrant(const QuestGrantPlan& acPlan, QuestGrantReceipt& aReceipt) noexcept;
    bool ReadQuestGrants(const CharacterIdentity& acRecipient, uint64_t aAfterRevision, TiltedPhoques::Vector<QuestGrantReceipt>& aReceipts) noexcept;
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
    QuestResult
    ChangeQuest(GameId aId, uint32_t aEpoch, uint8_t aStatus, uint16_t aStage, uint8_t aType, bool aAllowMisc, const QuestRunParticipants* apParticipants, bool aCommit) noexcept;
    TiltedPhoques::String Key(const char* apKind, GameId aId) const noexcept;
    bool LoadQuestState(const TiltedPhoques::String& acAdapters) noexcept;
    bool ReadValidQuestGrant(const WorldStore::Record& acRecord, QuestGrantReceipt& aReceipt) noexcept;
    bool ValidateQuestGrantIndexes() noexcept;
    bool ReadValidItemReceipt(const WorldStore::Record& acRecord, WorldItemReceipt& aReceipt) noexcept;
    bool ValidateItemReceiptIndexes() noexcept;
    WorldStore m_store;
    std::optional<Contract> m_contract;
    TiltedPhoques::String m_error;
    bool m_ready{};
    QuestAdapters m_adapters;
    TiltedPhoques::Vector<SavedQuest> m_quests;
    uint64_t m_questRevision{};
    bool m_bootstrapped{};
    uint64_t m_worldEpoch{};
};

// Checked arithmetic for authoritative snapshots; transfer acknowledgement is a separate layer.
bool ApplyInventoryDelta(Inventory& aInventory, const Inventory::Entry& acDelta) noexcept;
} // namespace Campaign
