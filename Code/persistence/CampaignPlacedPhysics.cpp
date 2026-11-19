#include "CampaignState.h"
#include <TiltedCore/ViewBuffer.hpp>
#include <algorithm>
#include <cstring>
#include <set>
namespace Campaign
{
namespace
{
TiltedPhoques::String PlacedKey(GameId id) noexcept
{
    return TiltedPhoques::String("placed-physics/") + std::to_string(id.ModId).c_str() + "/" + std::to_string(id.BaseId).c_str();
}
bool EncodePlaced(const PlacedPhysicsState& state, TiltedPhoques::Vector<uint8_t>& bytes) noexcept
{
    if (!state.IsValid())
        return false;
    TiltedPhoques::Buffer buffer(8192);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(1, 8);
    state.Serialize(writer);
    bytes.assign(buffer.GetWriteData(), buffer.GetWriteData() + writer.Size());
    return bytes.size() <= 8192;
}
bool DecodePlaced(const TiltedPhoques::Vector<uint8_t>& bytes, PlacedPhysicsState& state) noexcept
{
    if (bytes.empty() || bytes.size() > 8192)
        return false;
    TiltedPhoques::ViewBuffer view(const_cast<uint8_t*>(bytes.data()), bytes.size());
    TiltedPhoques::Buffer::Reader reader(&view);
    uint64_t version{};
    if (!reader.ReadBits(version, 8) || version != 1 || !state.Deserialize(reader))
        return false;
    TiltedPhoques::Vector<uint8_t> encoded;
    return EncodePlaced(state, encoded) && encoded == bytes;
}
} // namespace
bool State::LoadPlacedPhysics(TiltedPhoques::Vector<PlacedPhysicsState>& out) noexcept
{
    out.clear();
    TiltedPhoques::Vector<WorldStore::Record> records;
    if (!m_ready || !m_contract || !m_store.ReadPrefix("placed-physics/", records) || records.size() > PlacedPhysicsState::kMaxRecords)
    {
        m_error = "Cannot read the bounded placed-physics registry";
        return false;
    }
    for (const auto& record : records)
    {
        PlacedPhysicsState state;
        if (!DecodePlaced(record.Value, state) || record.Key != PlacedKey(state.Reference) || !ValidForm(state.Reference) || !ValidForm(state.Cell) ||
            !ValidForm(state.WorldSpace, true))
        {
            m_error = "Invalid saved placed-physics reference or pose";
            return false;
        }
        out.push_back(std::move(state));
    }
    return true;
}
bool State::SavePlacedPhysics(const TiltedPhoques::Vector<PlacedPhysicsState>& states) noexcept
{
    if (!m_ready || !m_contract || states.size() > PlacedPhysicsState::kMaxRecords)
        return false;
    TiltedPhoques::Vector<WorldStore::Record> records;
    std::set<TiltedPhoques::String> keys;
    for (const auto& state : states)
    {
        WorldStore::Record record;
        record.Key = PlacedKey(state.Reference);
        if (!ValidForm(state.Reference) || !ValidForm(state.Cell) || !ValidForm(state.WorldSpace, true) || !keys.insert(record.Key).second || !EncodePlaced(state, record.Value))
        {
            m_error = "Cannot save an invalid or duplicate placed-physics record";
            return false;
        }
        WorldStore::Record previous;
        const auto read = m_store.Read(record.Key, previous);
        if (read == WorldStore::ReadResult::Failed)
        {
            m_error = m_store.Error();
            return false;
        }
        if (read == WorldStore::ReadResult::Found)
            record.Revision = previous.Revision;
        records.push_back(std::move(record));
    }
    if (records.empty())
        return true;
    for (size_t start = 0; start < records.size(); start += 256)
    {
        const size_t end = std::min(records.size(), start + 256);
        TiltedPhoques::Vector<WorldStore::Record> page(records.begin() + start, records.begin() + end);
        uint64_t revision{};
        if (m_store.Commit(page, revision) != WorldStore::WriteResult::Committed)
        {
            m_error = m_store.Error();
            return false;
        }
    }
    return true;
}
} // namespace Campaign
