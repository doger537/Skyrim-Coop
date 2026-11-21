#include "CampaignState.h"

namespace Campaign
{
bool State::IsRestorePending(bool& aPending) noexcept
{
    if (!m_ready || !m_store.IsRestorePending(aPending))
    {
        m_error = m_store.Error();
        return false;
    }
    return true;
}
bool State::CompleteRestore() noexcept
{
    if (!m_ready || !m_store.CompleteRestore())
    {
        m_error = m_store.Error();
        return false;
    }
    return true;
}
bool State::LastRestoredCheckpoint(uint64_t& aId, uint64_t& aEpoch) noexcept
{
    if (!m_ready || !m_store.LastRestoredCheckpoint(aId, aEpoch))
    {
        m_error = m_store.Error();
        return false;
    }
    return true;
}
bool State::ReserveCheckpoint(uint64_t& aId) noexcept
{
    if (!m_ready || !m_contract || !m_bootstrapped || !m_worldEpoch || !m_store.ReserveCheckpoint(aId))
    {
        m_error = "Cannot reserve an initialized world checkpoint: " + m_store.Error();
        return false;
    }
    return true;
}
bool State::CaptureCheckpoint(uint64_t aId, const TiltedPhoques::Vector<uint8_t>& acMetadata) noexcept
{
    if (!m_ready || !m_contract || !m_bootstrapped || !m_worldEpoch || !m_store.CaptureCheckpoint(aId, acMetadata))
    {
        m_error = "Cannot commit the paired world checkpoint: " + m_store.Error();
        return false;
    }
    return true;
}
bool State::ReadCheckpoint(uint64_t aId, WorldStore::Checkpoint& aCheckpoint) noexcept
{
    if (!m_ready || !m_contract || !m_store.ReadCheckpoint(aId, aCheckpoint))
    {
        m_error = "Cannot verify the paired world checkpoint: " + m_store.Error();
        return false;
    }
    return true;
}
bool State::RestoreCheckpoint(uint64_t aId) noexcept
{
    if (!m_ready || !m_contract)
        return false;
    const auto adapters = m_adapters.Canonical();
    uint64_t epoch{};
    if (!m_store.RestoreCheckpoint(aId, epoch))
    {
        m_error = "Cannot restore the paired world checkpoint: " + m_store.Error();
        return false;
    }
    m_worldEpoch = epoch;
    if (!LoadQuestState(adapters) || !ValidateQuestGrantIndexes() || !ValidateItemReceiptIndexes())
    {
        // Do not continue a live server after the durable restore if rebuilding
        // its derived journal view fails. The committed epoch remains on disk.
        m_ready = false;
        return false;
    }
    return true;
}
}
