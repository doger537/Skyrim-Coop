#include "NotifyQuestJournal.h"
#include <TiltedCore/Serialization.hpp>

void NotifyQuestJournal::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Nonce);
    Serialization::WriteVarInt(aWriter, Revision);
    Data.Serialize(aWriter);
    aWriter.WriteBits(ReplayStages.size(), 13);
    for (uint16_t stage : ReplayStages)
        aWriter.WriteBits(stage, 16);
    Serialization::WriteBool(aWriter, ReplayLifecycle);
}

void NotifyQuestJournal::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Nonce = Serialization::ReadVarInt(aReader);
    Revision = Serialization::ReadVarInt(aReader);
    Data.Deserialize(aReader);
    if (!Data.IsValid())
        return;
    uint64_t count = 0;
    aReader.ReadBits(count, 13);
    if (count > QuestJournal::kMaxEvents)
    {
        Data.Decoded = false;
        return;
    }
    ReplayStages.resize(count);
    for (auto& stage : ReplayStages)
    {
        aReader.ReadBits(count, 16);
        stage = static_cast<uint16_t>(count);
    }
    ReplayLifecycle = Serialization::ReadBool(aReader);
}
