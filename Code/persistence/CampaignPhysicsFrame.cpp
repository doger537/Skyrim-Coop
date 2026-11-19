#include "CampaignState.h"
#include <common/PhysicsFrameHash.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <algorithm>
namespace Campaign
{
bool State::SavePhysicsFrame(const PhysicsFrame& frame, const PhysicsFrame::Digest& digest) noexcept
{
    Vector<uint8_t> bytes;
    if (!m_ready || !m_contract || !frame.Encode(bytes) || HashPhysicsFrame(bytes) != digest)
    {
        m_error = "Invalid canonical physical checkpoint";
        return false;
    }
    constexpr size_t chunkBytes = 256 * 1024;
    const size_t count = (bytes.size() + chunkBytes - 1) / chunkBytes;
    Vector<WorldStore::Record> records;
    WorldStore::Record head;
    head.Key = "$physics-frame";
    WorldStore::Record old;
    const auto read = m_store.Read(head.Key, old);
    if (read == WorldStore::ReadResult::Failed)
    {
        m_error = m_store.Error();
        return false;
    }
    if (read == WorldStore::ReadResult::Found)
        head.Revision = old.Revision;
    TiltedPhoques::Buffer buffer(128);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(1, 16);
    writer.WriteBits(frame.Checkpoint, 64);
    writer.WriteBits(bytes.size(), 32);
    writer.WriteBits(count, 16);
    for (auto byte : digest)
        writer.WriteBits(byte, 8);
    head.Value.assign(buffer.GetWriteData(), buffer.GetWriteData() + writer.Size());
    records.push_back(std::move(head));
    for (size_t i = 0; i < count; ++i)
    {
        WorldStore::Record part;
        part.Key = String("$physics-frame/") + std::to_string(i).c_str();
        const auto found = m_store.Read(part.Key, old);
        if (found == WorldStore::ReadResult::Failed)
        {
            m_error = m_store.Error();
            return false;
        }
        if (found == WorldStore::ReadResult::Found)
            part.Revision = old.Revision;
        const size_t start = i * chunkBytes, end = std::min(bytes.size(), start + chunkBytes);
        part.Value.assign(bytes.begin() + start, bytes.begin() + end);
        records.push_back(std::move(part));
    }
    uint64_t revision{};
    if (m_store.Commit(records, revision) != WorldStore::WriteResult::Committed)
    {
        m_error = m_store.Error();
        return false;
    }
    return true;
}
bool State::ReadPhysicsFrame(uint64_t id, const PhysicsFrame::Digest& expected, PhysicsFrame& frame) noexcept
{
    WorldStore::Record head;
    if (!m_ready || m_store.Read("$physics-frame", head) != WorldStore::ReadResult::Found || head.Value.size() != 48)
    {
        m_error = "Physical checkpoint header missing or invalid";
        return false;
    }
    TiltedPhoques::ViewBuffer view(head.Value.data(), head.Value.size());
    TiltedPhoques::Buffer::Reader reader(&view);
    uint64_t version{}, saved{}, size{}, count{};
    PhysicsFrame::Digest hash{};
    if (!reader.ReadBits(version, 16) || version != 1 || !reader.ReadBits(saved, 64) || saved != id || !reader.ReadBits(size, 32) || !reader.ReadBits(count, 16) || !size ||
        size > PhysicsFrame::kMaxBytes || count != (size + 256 * 1024 - 1) / (256 * 1024))
    {
        m_error = "Invalid physical checkpoint boundary";
        return false;
    }
    for (auto& byte : hash)
    {
        uint64_t value{};
        if (!reader.ReadBits(value, 8))
            return false;
        byte = static_cast<uint8_t>(value);
    }
    if (hash != expected)
    {
        m_error = "Physical checkpoint differs from paired manifest";
        return false;
    }
    Vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(size));
    for (uint64_t i = 0; i < count; ++i)
    {
        WorldStore::Record part;
        if (m_store.Read(String("$physics-frame/") + std::to_string(i).c_str(), part) != WorldStore::ReadResult::Found || part.Revision != head.Revision ||
            part.Value.size() != std::min(uint64_t(256 * 1024), size - bytes.size()))
        {
            m_error = "Incomplete physical checkpoint chunk transaction";
            return false;
        }
        bytes.insert(bytes.end(), part.Value.begin(), part.Value.end());
    }
    if (HashPhysicsFrame(bytes) != hash || !PhysicsFrame::Decode(bytes, frame) || frame.Checkpoint != id)
    {
        m_error = "Physical checkpoint digest or codec mismatch";
        return false;
    }
    return true;
}
} // namespace Campaign
