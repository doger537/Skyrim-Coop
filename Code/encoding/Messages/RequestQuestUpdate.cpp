#include <Messages/RequestQuestUpdate.h>

void RequestQuestUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Id.ModId, 32);
    aWriter.WriteBits(Id.BaseId, 32);
    aWriter.WriteBits(Stage, 16);
    aWriter.WriteBits(Status, 8);
    aWriter.WriteBits(ClientQuestType, 8);
    aWriter.WriteBits(Epoch, 32);
}
void RequestQuestUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t mod{}, base{}, stage{}, status{}, type{}, epoch{};
    if (!aReader.ReadBits(mod, 32) ||
        !aReader.ReadBits(base, 32) ||
        !aReader.ReadBits(stage, 16) ||
        !aReader.ReadBits(status, 8) ||
        !aReader.ReadBits(type, 8) ||
        !aReader.ReadBits(epoch, 32)) return;
    Id = GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base));
    Stage = static_cast<uint16_t>(stage); Status = static_cast<uint8_t>(status);
    ClientQuestType = static_cast<uint8_t>(type); Epoch = static_cast<uint32_t>(epoch);
    Decoded = true; Decoded = IsValid();
}
