#include "PhysicsSaveMap.h"
#include <TiltedCore/ViewBuffer.hpp>
#include <fstream>
#include <algorithm>
#include <set>
namespace PhysicsSaveMap
{
namespace
{
bool Valid(const Document& doc) noexcept
{
    std::set<uint32_t> forms;
    PhysicsFrameEntry previous;
    bool first = true;
    for (const auto& entry : doc.Entries)
    {
        PhysicsFrameEntry key;
        key.Type = entry.Type;
        key.Reference = entry.Reference;
        key.Id = entry.Id;
        if (!forms.insert(entry.LocalForm).second || (!first && !PhysicsFrameEntry::Less(previous, key)))
            return false;
        previous = key;
        first = false;
    }
    return doc.Checkpoint && doc.Character.IsValid() && doc.World.size() == 32 && doc.Entries.size() <= PhysicsFrame::kMaxEntries &&
           std::all_of(doc.World.begin(), doc.World.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
           std::any_of(doc.Canonical.begin(), doc.Canonical.end(), [](uint8_t b) { return b != 0; }) &&
           std::all_of(
               doc.Entries.begin(), doc.Entries.end(),
               [](const auto& e)
               {
                   return e.Type <= PhysicsFrameEntry::Actor && e.LocalForm &&
                          (e.Type == PhysicsFrameEntry::Placed ? bool(e.Reference) && !e.Id : !e.Reference && (e.Type == PhysicsFrameEntry::Drop ? e.Id != 0 : e.Id < UINT32_MAX));
               });
}
bool Encode(const Document& doc, Vector<uint8_t>& bytes) noexcept
{
    if (!Valid(doc))
        return false;
    TiltedPhoques::Buffer buffer(1024 * 1024);
    TiltedPhoques::Buffer::Writer w(&buffer);
    w.WriteBits(0x31504D4354ull, 64);
    w.WriteBits(doc.Checkpoint, 64);
    doc.Character.Serialize(w);
    for (char c : doc.World)
        w.WriteBits(static_cast<uint8_t>(c), 8);
    for (auto b : doc.Canonical)
        w.WriteBits(b, 8);
    w.WriteBits(doc.Entries.size(), 32);
    for (const auto& e : doc.Entries)
    {
        w.WriteBits(e.Type, 8);
        w.WriteBits(e.Reference.ModId, 32);
        w.WriteBits(e.Reference.BaseId, 32);
        w.WriteBits(e.Id, 64);
        w.WriteBits(e.LocalForm, 32);
    }
    bytes.assign(buffer.GetWriteData(), buffer.GetWriteData() + w.Size());
    return bytes.size() <= 1024 * 1024;
}
} // namespace
bool Write(const std::filesystem::path& path, const Document& doc) noexcept
{
    Vector<uint8_t> bytes;
    if (path.empty() || !Encode(doc, bytes))
        return false;
    std::error_code error;
    if (std::filesystem::exists(path, error) || error)
        return false;
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.flush();
        if (!out)
            return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}
bool Read(const std::filesystem::path& path, Document& doc) noexcept
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || !size || size > 1024 * 1024)
        return false;
    Vector<uint8_t> bytes(static_cast<size_t>(size));
    {
        std::ifstream in(path, std::ios::binary);
        if (!in || !in.read(reinterpret_cast<char*>(bytes.data()), size))
            return false;
    }
    TiltedPhoques::ViewBuffer view(bytes.data(), bytes.size());
    TiltedPhoques::Buffer::Reader r(&view);
    uint64_t magic{}, count{}, v{};
    Document result;
    if (!r.ReadBits(magic, 64) || magic != 0x31504D4354ull || !r.ReadBits(result.Checkpoint, 64) || !result.Character.Deserialize(r))
        return false;
    for (size_t i = 0; i < 32; ++i)
    {
        if (!r.ReadBits(v, 8))
            return false;
        result.World += static_cast<char>(v);
    }
    for (auto& b : result.Canonical)
    {
        if (!r.ReadBits(v, 8))
            return false;
        b = static_cast<uint8_t>(v);
    }
    if (!r.ReadBits(count, 32) || count > PhysicsFrame::kMaxEntries)
        return false;
    for (uint64_t i = 0; i < count; ++i)
    {
        Entry e;
        uint64_t type{}, m{}, b{}, form{};
        if (!r.ReadBits(type, 8) || !r.ReadBits(m, 32) || !r.ReadBits(b, 32) || !r.ReadBits(e.Id, 64) || !r.ReadBits(form, 32))
            return false;
        e.Type = static_cast<uint8_t>(type);
        e.Reference = GameId(static_cast<uint32_t>(m), static_cast<uint32_t>(b));
        e.LocalForm = static_cast<uint32_t>(form);
        result.Entries.push_back(e);
    }
    Vector<uint8_t> check;
    if (!Encode(result, check) || bytes != check)
        return false;
    doc = std::move(result);
    return true;
}
} // namespace PhysicsSaveMap
