#include "NotifyPartyLocations.h"
#include <cmath>

bool NotifyPartyLocations::Location::IsValid(uint64_t aSnapshotTick) const noexcept
{
    return ServerId != UINT32_MAX && OwnershipEpoch != 0 && Cell.BaseId != 0 && Cell.ModId != UINT32_MAX &&
           Cell.BaseId <= 0xFFFFFF && WorldSpace.ModId != UINT32_MAX && WorldSpace.BaseId <= 0xFFFFFF &&
           (WorldSpace.BaseId != 0 || WorldSpace.ModId == 0) && ReceivedTick <= aSnapshotTick && aSnapshotTick - ReceivedTick <= kMaxAgeMs &&
           std::isfinite(Position.x) && std::isfinite(Position.y) && std::isfinite(Position.z) &&
           std::abs(Position.x) <= 1048575.f && std::abs(Position.y) <= 1048575.f && std::abs(Position.z) <= 1048575.f;
}
bool NotifyPartyLocations::IsValid() const noexcept
{
    if (!Decoded || Sequence == 0 || Players.size() > kMaxPlayers)
        return false;
    for (size_t i = 0; i < Players.size(); ++i)
    {
        if (!Players[i].IsValid(Tick))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (Players[i].PlayerId == Players[j].PlayerId || Players[i].ServerId == Players[j].ServerId)
                return false;
    }
    return true;
}
void NotifyPartyLocations::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid(), 1);
    if (!IsValid())
        return;
    aWriter.WriteBits(LeaderPlayerId, 32);
    aWriter.WriteBits(Sequence, 64);
    aWriter.WriteBits(Tick, 64);
    aWriter.WriteBits(Players.size(), 8);
    for (const auto& player : Players)
    {
        aWriter.WriteBits(player.PlayerId, 32);
        aWriter.WriteBits(player.ServerId, 32);
        aWriter.WriteBits(player.OwnershipEpoch, 32);
        aWriter.WriteBits(player.ReceivedTick, 64);
        aWriter.WriteBits(player.Cell.ModId, 32);
        aWriter.WriteBits(player.Cell.BaseId, 32);
        aWriter.WriteBits(player.WorldSpace.ModId, 32);
        aWriter.WriteBits(player.WorldSpace.BaseId, 32);
        aWriter.WriteBits(player.Position.Pack(), 64);
    }
}
void NotifyPartyLocations::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    Players.clear();
    uint64_t value{};
    if (!aReader.ReadBits(value, 1) || value == 0 || !aReader.ReadBits(value, 32))
        return;
    LeaderPlayerId = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(Sequence, 64) || !aReader.ReadBits(Tick, 64) || !aReader.ReadBits(value, 8) || value > kMaxPlayers)
        return;
    Players.resize(static_cast<size_t>(value));
    const auto number = [&](uint32_t& aOutput)
    {
        if (!aReader.ReadBits(value, 32))
            return false;
        aOutput = static_cast<uint32_t>(value);
        return true;
    };
    for (auto& player : Players)
    {
        if (!number(player.PlayerId) || !number(player.ServerId) || !number(player.OwnershipEpoch) || !aReader.ReadBits(player.ReceivedTick, 64) ||
            !number(player.Cell.ModId) || !number(player.Cell.BaseId) || !number(player.WorldSpace.ModId) || !number(player.WorldSpace.BaseId) ||
            !aReader.ReadBits(value, 64) || (value >> 63) != 0)
            return;
        player.Position.Unpack(value);
    }
    Decoded = true;
    Decoded = IsValid();
}
