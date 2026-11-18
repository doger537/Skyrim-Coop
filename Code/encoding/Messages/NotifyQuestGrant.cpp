#include "NotifyQuestGrant.h"
void NotifyQuestGrant::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Nonce, 64); aWriter.WriteBits(AfterRevision, 64); aWriter.WriteBits(HeadRevision, 64); aWriter.WriteBits(ReceiptRevision, 64);
    aWriter.WriteBits(Payload.size(), 32);
    for (uint8_t value : Payload) aWriter.WriteBits(value, 8);
}
void NotifyQuestGrant::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false; Payload.clear();
    uint64_t count{};
    if (!aReader.ReadBits(Nonce, 64) || !aReader.ReadBits(AfterRevision, 64) || !aReader.ReadBits(HeadRevision, 64) ||
        !aReader.ReadBits(ReceiptRevision, 64) || !aReader.ReadBits(count, 32) || count > kMaxBytes) return;
    Payload.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t value{};
        if (!aReader.ReadBits(value, 8)) { Payload.clear(); return; }
        Payload.push_back(static_cast<uint8_t>(value));
    }
    Decoded = true; Decoded = IsValid();
}
