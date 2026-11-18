#include "RequestQuestDecision.h"

void RequestQuestDecision::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Proposal, 64);
    aWriter.WriteBits(Accepted, 8);
}
void RequestQuestDecision::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t accepted{};
    Decoded = aReader.ReadBits(Proposal, 64) && aReader.ReadBits(accepted, 8) && accepted <= 1;
    Accepted = accepted == 1;
    Decoded = IsValid();
}
