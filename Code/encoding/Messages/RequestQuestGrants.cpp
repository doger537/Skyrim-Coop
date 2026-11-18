#include "RequestQuestGrants.h"
void RequestQuestGrants::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Nonce, 64); aWriter.WriteBits(AfterRevision, 64); aWriter.WriteBits(CursorAvailable, 8);
}
void RequestQuestGrants::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t available{};
    Decoded = aReader.ReadBits(Nonce, 64) && aReader.ReadBits(AfterRevision, 64) && aReader.ReadBits(available, 8) && available <= 1;
    CursorAvailable = available == 1;
    Decoded = IsValid();
}
