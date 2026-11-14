#include "QuestJournal.h"
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool QuestJournal::IsValid() const noexcept
{
    return Decoded && Epoch != 0 && Type <= 11 && Events.size() <= kMaxEvents && Decisions.size() <= 256 &&
           std::all_of(Events.begin(), Events.end(), [](const auto& event) { return event.Status <= 2; }) &&
           std::all_of(Decisions.begin(), Decisions.end(), [](const auto& decision) { return !decision.Group.empty() && decision.Group.size() <= 64; });
}

void QuestJournal::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const bool valid = IsValid();
    Serialization::WriteBool(aWriter, valid);
    if (!valid)
        return;
    Id.Serialize(aWriter);
    aWriter.WriteBits(Epoch, 32);
    aWriter.WriteBits(Type, 8);
    Serialization::WriteBool(aWriter, Started);
    Serialization::WriteBool(aWriter, Stopped);
    aWriter.WriteBits(Events.size(), 13);
    for (const auto& event : Events)
    {
        aWriter.WriteBits(event.Status, 2);
        aWriter.WriteBits(event.Stage, 16);
        Serialization::WriteBool(aWriter, event.Bootstrap);
    }
    aWriter.WriteBits(Decisions.size(), 9);
    for (const auto& decision : Decisions)
    {
        Serialization::WriteString(aWriter, decision.Group);
        aWriter.WriteBits(decision.Stage, 16);
    }
}

void QuestJournal::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    Decoded = Serialization::ReadBool(aReader);
    if (!Decoded)
        return;
    Id.Deserialize(aReader);
    uint64_t data = 0;
    aReader.ReadBits(data, 32);
    Epoch = static_cast<uint32_t>(data);
    aReader.ReadBits(data, 8);
    Type = static_cast<uint8_t>(data);
    Started = Serialization::ReadBool(aReader);
    Stopped = Serialization::ReadBool(aReader);
    aReader.ReadBits(data, 13);
    if (data > kMaxEvents)
    {
        Decoded = false;
        return;
    }
    Events.resize(data);
    for (auto& event : Events)
    {
        aReader.ReadBits(data, 2);
        event.Status = static_cast<uint8_t>(data);
        aReader.ReadBits(data, 16);
        event.Stage = static_cast<uint16_t>(data);
        event.Bootstrap = Serialization::ReadBool(aReader);
    }
    aReader.ReadBits(data, 9);
    if (data > 256)
    {
        Decoded = false;
        return;
    }
    Decisions.resize(data);
    for (auto& decision : Decisions)
    {
        decision.Group = Serialization::ReadString(aReader);
        aReader.ReadBits(data, 16);
        decision.Stage = static_cast<uint16_t>(data);
    }
    Decoded = IsValid();
}
