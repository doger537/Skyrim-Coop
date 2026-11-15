#include "CampaignState.h"
#include <Structs/AlternateStartPolicy.h>
#include <Structs/ContentCompatibility.h>
#include <algorithm>
#include <limits>
#include <set>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

namespace Campaign
{
namespace
{
bool HasStage(const Quest& acQuest, uint16_t aStage) noexcept
{
    return std::any_of(acQuest.Events.begin(), acQuest.Events.end(), [&](const auto& event) { return event.Status == 0 && event.Stage == aStage; });
}

bool Choose(Quest& aQuest, const QuestAdapter* apAdapter, uint16_t aStage, String& aError) noexcept
{
    if (!apAdapter)
        return true;
    if (std::find(apAdapter->Stages.begin(), apAdapter->Stages.end(), aStage) == apAdapter->Stages.end())
    {
        aError = "Quest stage is absent from its pinned adapter";
        return false;
    }
    for (const auto& branch : apAdapter->Branches)
    {
        if (std::find(branch.Stages.begin(), branch.Stages.end(), aStage) == branch.Stages.end())
            continue;
        const auto decision = std::find_if(aQuest.Decisions.begin(), aQuest.Decisions.end(), [&](const auto& value) { return value.Group == branch.Id; });
        if (decision != aQuest.Decisions.end() && decision->Stage != aStage)
        {
            aError = "Shared story decision '" + branch.Id + "' is already committed at stage " + std::to_string(decision->Stage).c_str();
            return false;
        }
        if (decision == aQuest.Decisions.end())
            aQuest.Decisions.push_back({branch.Id, aStage});
    }
    return true;
}
} // namespace

const QuestAdapter* State::Adapter(GameId aId) const noexcept
{
    return ValidForm(aId) ? m_adapters.Find(m_contract->Content.ModList[aId.ModId].Filename, aId.BaseId) : nullptr;
}

const State::SavedQuest* State::FindQuest(GameId aId) const noexcept
{
    const auto found = std::find_if(m_quests.begin(), m_quests.end(), [&](const auto& quest) { return quest.Data.Id == aId; });
    return found == m_quests.end() ? nullptr : &*found;
}

Vector<GameId> State::MiscQuestIds() const noexcept
{
    Vector<GameId> result;
    if (!m_contract)
        return result;
    for (const auto& adapter : m_adapters.Entries())
        if (adapter.AllowMisc)
            for (size_t i = 0; i < m_contract->Content.ModList.size(); ++i)
                if (ContentCompatibility::NormalizeName(m_contract->Content.ModList[i].Filename) == adapter.Plugin)
                    result.emplace_back(static_cast<uint32_t>(i), adapter.Form);
    return result;
}

bool State::LoadQuestState(const String& acAdapters) noexcept
{
    m_quests.clear();
    m_questRevision = 0;
    m_bootstrapped = false;
    if (!m_adapters.Load(std::string_view(acAdapters.data(), acAdapters.size())) || (m_contract && !m_adapters.Validate(m_contract->Content)))
    {
        m_error = m_adapters.Error();
        return false;
    }
    if (!m_contract)
        return true;
    WorldStore::Record adapters;
    const auto read = m_store.Read("$adapters", adapters);
    const auto& canonical = m_adapters.Canonical();
    const Vector<uint8_t> expected(canonical.begin(), canonical.end());
    if (read == WorldStore::ReadResult::Failed || (read == WorldStore::ReadResult::Found && adapters.Value != expected))
    {
        m_error = "Saved quest adapter policy differs or is unreadable. Use the pinned policy; do not reset the world";
        return false;
    }
    if (read == WorldStore::ReadResult::Missing)
    {
        // H worlds have containers and a contract, but no quest policy yet.
        adapters.Key = "$adapters";
        adapters.Value = expected;
        uint64_t revision = 0;
        if (m_store.Commit({adapters}, revision) != WorldStore::WriteResult::Committed)
        {
            m_error = m_store.Error();
            return false;
        }
    }
    Vector<WorldStore::Record> records;
    if (!m_store.ReadPrefix("quest/", records) || records.size() > 4095)
    {
        m_error = "Shared quest journal is unreadable or exceeds the supported quest count";
        return false;
    }
    for (const auto& record : records)
    {
        Quest quest;
        if (!Decode(record.Value, quest) || !quest.IsValid() || !ValidForm(quest.Id) || record.Key != Key("quest", quest.Id) || FindQuest(quest.Id))
        {
            m_error = "Saved shared quest entry is invalid";
            return false;
        }
        m_questRevision = std::max(m_questRevision, record.Revision);
        m_quests.push_back({std::move(quest), record.Revision});
    }
    WorldStore::Record seeded;
    const auto seedRead = m_store.Read("$seeded", seeded);
    if (seedRead == WorldStore::ReadResult::Failed || (seedRead == WorldStore::ReadResult::Found && seeded.Value != Vector<uint8_t>{1}) ||
        (seedRead == WorldStore::ReadResult::Missing && !m_quests.empty()))
    {
        m_error = "Shared campaign baseline is incomplete or invalid";
        return false;
    }
    m_bootstrapped = seedRead == WorldStore::ReadResult::Found;
    if (m_bootstrapped)
        m_questRevision = std::max(m_questRevision, seeded.Revision);
    return true;
}

bool State::BootstrapQuests(const Vector<Quest>& acQuests, bool aAllowMisc) noexcept
{
    if (!m_ready || !m_contract || m_bootstrapped || acQuests.size() > 4095)
    {
        m_error = "The shared campaign can be initialized only once";
        return false;
    }
    std::set<uint64_t> ids;
    Vector<WorldStore::Record> records;
    Vector<SavedQuest> quests;
    for (auto quest : acQuests)
    {
        const auto* adapter = Adapter(quest.Id);
        if (!quest.IsValid() || !ValidForm(quest.Id) || quest.Epoch != 1 || !ids.insert(quest.Id.LogFormat()).second || !quest.Decisions.empty() ||
            ContentCompatibility::NormalizeName(m_contract->Content.ModList[quest.Id.ModId].Filename) == AlternateStartPolicy::cPlugin ||
            ((quest.Type == 0 || quest.Type == 6) && !aAllowMisc && !(adapter && adapter->AllowMisc)))
        {
            m_error = "Initial quest baseline has an invalid, duplicate, personal or unsupported miscellaneous quest";
            return false;
        }
        std::set<uint16_t> stages;
        for (const auto& event : quest.Events)
        {
            if (!event.Bootstrap || (event.Status == 0 && !stages.insert(event.Stage).second))
            {
                m_error = "Initial quest baseline has a duplicate stage or a non-imported event";
                return false;
            }
            if (event.Status != 2 && !Choose(quest, adapter, event.Stage, m_error))
                return false;
        }
        WorldStore::Record record;
        record.Key = Key("quest", quest.Id);
        if (!Encode(quest, record.Value))
        {
            m_error = "Initial quest baseline cannot be encoded";
            return false;
        }
        records.push_back(std::move(record));
        quests.push_back({std::move(quest), 0});
    }
    records.push_back({"$seeded", {1}, 0});
    uint64_t revision = 0;
    if (m_store.Commit(records, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return false;
    }
    for (auto& quest : quests)
        quest.Revision = revision;
    m_quests = std::move(quests);
    m_bootstrapped = true;
    m_questRevision = revision;
    return true;
}

State::QuestResult State::UpdateQuest(GameId aId, uint32_t aEpoch, uint8_t aStatus, uint16_t aStage, uint8_t aType, bool aAllowMisc) noexcept
{
    if (!m_ready || !m_bootstrapped || !ValidForm(aId) || aEpoch == 0 || aStatus > 2 || aType > 11)
    {
        m_error = "Invalid shared quest identity, epoch or state";
        return QuestResult::Invalid;
    }
    if (ContentCompatibility::NormalizeName(m_contract->Content.ModList[aId.ModId].Filename) == AlternateStartPolicy::cPlugin)
        return QuestResult::Ignored;
    const auto* adapter = Adapter(aId);
    if ((aType == 0 || aType == 6) && !aAllowMisc && !(adapter && adapter->AllowMisc))
        return QuestResult::Ignored;
    const auto* saved = FindQuest(aId);
    Quest next = saved ? saved->Data : Quest{};
    next.Id = aId;
    next.Type = aType;
    if (saved && saved->Data.Type != aType)
    {
        m_error = "Quest type differs from the saved campaign";
        return QuestResult::Invalid;
    }
    if (aStatus == 1 && next.Stopped && next.Epoch < UINT32_MAX && aEpoch == next.Epoch + 1)
    {
        if (adapter && !adapter->AllowRestart)
        {
            m_error = "The pinned quest adapter forbids restarting this story quest";
            return QuestResult::Conflict;
        }
        next = {};
        next.Id = aId;
        next.Type = aType;
        next.Epoch = aEpoch;
    }
    if (next.Epoch != aEpoch)
    {
        m_error = "Quest run epoch differs from the shared campaign";
        return QuestResult::Conflict;
    }
    if ((aStatus == 0 && HasStage(next, aStage)) || (aStatus == 1 && next.Started) || (aStatus == 2 && next.Stopped))
        return QuestResult::Duplicate;
    if (next.Stopped)
    {
        m_error = "Quest is stopped; a new run requires the next epoch";
        return QuestResult::Conflict;
    }
    if (aStatus != 2 && !Choose(next, adapter, aStage, m_error))
        return QuestResult::Conflict;
    if (aStatus == 1)
        next.Started = true;
    if (aStatus == 2)
        next.Stopped = true;
    if (next.Events.size() >= QuestJournal::kMaxEvents || (!saved && m_quests.size() >= 4095))
    {
        m_error = "Shared quest journal exceeded its bounded event/quest count";
        return QuestResult::Invalid;
    }
    next.Events.push_back({aStatus, aStage, false});
    WorldStore::Record state;
    state.Key = Key("quest", aId);
    state.Revision = saved ? saved->Revision : 0;
    Quest event = next;
    event.Events = {next.Events.back()};
    WorldStore::Record audit;
    audit.Key = Key("quest-event", aId) + "/" + std::to_string(aEpoch).c_str() + "/" + std::to_string(aStatus).c_str() + "/" + std::to_string(aStage).c_str();
    if (!Encode(next, state.Value) || !Encode(event, audit.Value))
    {
        m_error = "Cannot encode a shared quest update";
        return QuestResult::Invalid;
    }
    uint64_t revision = 0;
    if (m_store.Commit({state, audit}, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return QuestResult::StorageError;
    }
    const auto entry = std::find_if(m_quests.begin(), m_quests.end(), [&](const auto& quest) { return quest.Data.Id == aId; });
    if (entry == m_quests.end())
        m_quests.push_back({std::move(next), revision});
    else
        *entry = {std::move(next), revision};
    m_questRevision = revision;
    return QuestResult::Applied;
}
} // namespace Campaign
