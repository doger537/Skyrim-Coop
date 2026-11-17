#include <Messages/RequestWorldCheckpoint.h>

bool RequestWorldCheckpoint::IsValid() const noexcept
{
    if (!Decoded || Action > Kind::Failed)
        return false;
    if (Action == Kind::Save)
        return !Id && !Nonce;
    if (Action == Kind::Load)
        return Id && !Nonce;
    return Id && Nonce && (Action != Kind::Saved || Contribution.IsValid());
}
void RequestWorldCheckpoint::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Action), 8);
    aWriter.WriteBits(Id, 64);
    aWriter.WriteBits(Nonce, 64);
    if (Action == Kind::Saved)
        Contribution.Serialize(aWriter);
}
void RequestWorldCheckpoint::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t action{};
    if (!aReader.ReadBits(action, 8) || action > static_cast<uint8_t>(Kind::Failed) || !aReader.ReadBits(Id, 64) || !aReader.ReadBits(Nonce, 64))
        return;
    Action = static_cast<Kind>(action);
    if (Action == Kind::Saved && !Contribution.Deserialize(aReader))
        return;
    Decoded = true;
    Decoded = IsValid();
}
