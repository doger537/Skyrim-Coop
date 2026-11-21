#include "NotifyQuestProposal.h"

void NotifyQuestProposal::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Proposal, 64);
    aWriter.WriteBits(Id.ModId, 32);
    aWriter.WriteBits(Id.BaseId, 32);
    aWriter.WriteBits(Epoch, 32);
    aWriter.WriteBits(Stage, 16);
    aWriter.WriteBits(Status, 8);
    aWriter.WriteBits(QuestType, 8);
}
void NotifyQuestProposal::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    if (!aReader.ReadBits(Proposal, 64)) return;
    // GameId's legacy decoder does not report truncation; use fixed checked fields.
    uint64_t mod{}, base{}, epoch{}, stage{}, status{}, type{};
    if (!aReader.ReadBits(mod, 32) || !aReader.ReadBits(base, 32) || !aReader.ReadBits(epoch, 32) ||
        !aReader.ReadBits(stage, 16) || !aReader.ReadBits(status, 8) || !aReader.ReadBits(type, 8)) return;
    Id = GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base));
    Epoch = static_cast<uint32_t>(epoch); Stage = static_cast<uint16_t>(stage);
    Status = static_cast<uint8_t>(status); QuestType = static_cast<uint8_t>(type);
    Decoded = true; Decoded = IsValid();
}
