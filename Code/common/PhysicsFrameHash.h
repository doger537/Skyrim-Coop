#pragma once
#include <Structs/PhysicsFrame.h>
#include <cryptopp/sha.h>
inline PhysicsFrame::Digest HashPhysicsFrame(const TiltedPhoques::Vector<uint8_t>& bytes) noexcept
{
    PhysicsFrame::Digest hash{};
    CryptoPP::SHA256 digest;
    digest.CalculateDigest(hash.data(), bytes.data(), bytes.size());
    return hash;
}
