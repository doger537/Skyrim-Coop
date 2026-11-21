#pragma once
#include <Structs/PhysicsFrame.h>
#include <Structs/CharacterIdentity.h>
#include <filesystem>
namespace PhysicsSaveMap
{
struct Entry
{
    uint8_t Type{};
    GameId Reference;
    uint64_t Id{};
    uint32_t LocalForm{};
};
struct Document
{
    String World;
    CharacterIdentity Character;
    uint64_t Checkpoint{};
    PhysicsFrame::Digest Canonical{};
    Vector<Entry> Entries;
};
// File codec only: local ESS IDs never become network object identities.
bool Write(const std::filesystem::path&, const Document&) noexcept;
bool Read(const std::filesystem::path&, Document&) noexcept;
} // namespace PhysicsSaveMap
