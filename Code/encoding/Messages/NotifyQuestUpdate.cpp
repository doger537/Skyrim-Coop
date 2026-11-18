#include <Messages/NotifyQuestUpdate.h>

void NotifyQuestUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Id.ModId, 32);
    aWriter.WriteBits(Id.BaseId, 32);
    aWriter.WriteBits(Stage, 16);
    aWriter.WriteBits(Status, 8);
    aWriter.WriteBits(ClientQuestType, 8);
    aWriter.WriteBits(Epoch, 32);
    aWriter.WriteBits(Revision, 64);
    aWriter.WriteBits(RunStopped, 8);
    aWriter.WriteBits(AcknowledgementOnly, 8);
}
void NotifyQuestUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t mod{}, base{}, stage{}, status{}, type{}, epoch{}, revision{}, stopped{}, ack{};
    if (!aReader.ReadBits(mod, 32) ||
        !aReader.ReadBits(base, 32) ||
        !aReader.ReadBits(stage, 16) ||
        !aReader.ReadBits(status, 8) ||
        !aReader.ReadBits(type, 8) ||
        !aReader.ReadBits(epoch, 32) ||
        !aReader.ReadBits(revision, 64) ||
        !aReader.ReadBits(stopped, 8) ||
        !aReader.ReadBits(ack, 8)) return;
    Id = GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base));
    Stage = static_cast<uint16_t>(stage); Status = static_cast<uint8_t>(status);
    ClientQuestType = static_cast<uint8_t>(type); Epoch = static_cast<uint32_t>(epoch);
    if (stopped > 1 || ack > 1) return;
    Revision = revision; RunStopped = stopped == 1; AcknowledgementOnly = ack == 1;
    Decoded = true; Decoded = IsValid();
}
