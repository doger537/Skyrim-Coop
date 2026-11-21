#include <Messages/NotifyWorldCheckpoint.h>
#include <algorithm>

bool NotifyWorldCheckpoint::IsValid() const noexcept
{
    if (!Decoded || State > Phase::Cancelled || Failure > Error::Storage || !PreviousEpoch || WorldId.size() != 32 ||
        !std::all_of(WorldId.begin(), WorldId.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        return false;
    if (State == Phase::Cancelled)
        return Failure != Error::None;
    if ((State == Phase::SaveNow || State == Phase::AlignLoad || State == Phase::Resume) &&
        !std::any_of(PhysicsDigest.begin(), PhysicsDigest.end(), [](uint8_t b) { return b != 0; }))
        return false;
    return Failure == Error::None && Id && Nonce && Calendar.IsValid() && Members[0].IsValid() && Members[1].IsValid() && Members[0] != Members[1] &&
           (!HasManifest() || (Manifest.IsValid() && Manifest.PhysicsDigest == PhysicsDigest && Manifest.Id == Id && Manifest.Players[0].Character == Members[0] &&
                               Manifest.Players[1].Character == Members[1]));
}
void NotifyWorldCheckpoint::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(State), 8);
    aWriter.WriteBits(static_cast<uint8_t>(Failure), 8);
    aWriter.WriteBits(Id, 64);
    aWriter.WriteBits(Nonce, 64);
    aWriter.WriteBits(PreviousEpoch, 64);
    for (size_t i = 0; i < 32; ++i)
        aWriter.WriteBits(i < WorldId.size() ? static_cast<uint8_t>(WorldId[i]) : 0, 8);
    if (State != Phase::Cancelled)
    {
        Calendar.Serialize(aWriter);
        for (auto b : PhysicsDigest)
            aWriter.WriteBits(b, 8);
        for (const auto& member : Members)
            member.Serialize(aWriter);
        if (HasManifest())
            Manifest.Serialize(aWriter);
    }
}
void NotifyWorldCheckpoint::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 8) || value > static_cast<uint8_t>(Phase::Cancelled))
        return;
    State = static_cast<Phase>(value);
    if (!aReader.ReadBits(value, 8) || value > static_cast<uint8_t>(Error::Storage))
        return;
    Failure = static_cast<Error>(value);
    if (!aReader.ReadBits(Id, 64) || !aReader.ReadBits(Nonce, 64) || !aReader.ReadBits(PreviousEpoch, 64))
        return;
    WorldId.clear();
    for (size_t i = 0; i < 32; ++i)
    {
        if (!aReader.ReadBits(value, 8))
            return;
        WorldId += static_cast<char>(value);
    }
    if (State != Phase::Cancelled)
    {
        Calendar.Deserialize(aReader);
        for (auto& b : PhysicsDigest)
        {
            if (!aReader.ReadBits(value, 8))
                return;
            b = static_cast<uint8_t>(value);
        }
        for (auto& member : Members)
            if (!member.Deserialize(aReader))
                return;
        if (HasManifest())
            Manifest.Deserialize(aReader);
    }
    Decoded = true;
    Decoded = IsValid();
}
