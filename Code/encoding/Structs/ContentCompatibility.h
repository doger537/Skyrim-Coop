#pragma once

#include "Mods.h"
#include "GameId.h"

namespace ContentCompatibility
{
// Leave 16 KiB of the transport's 64 KiB packet for authentication's other fields.
constexpr size_t kMaxManifestBytes = 48 * 1024;

enum class Issue : uint8_t
{
    None,
    InvalidManifest,
    UnreadablePlugin,
    MissingPlugin,
    ExtraPlugin,
    DifferentPlugin,
    DifferentOrder,
    MissingDependency
};

struct Result
{
    Issue Reason{Issue::None};
    String Filename;
    explicit operator bool() const noexcept { return Reason == Issue::None; }
};

String NormalizeName(const String& acName) noexcept;
Result Validate(const Mods& acMods) noexcept;
Result Compare(const Mods& acExpected, const Mods& acActual) noexcept;
String Describe(const Result& acResult) noexcept;
String EscapeJson(const String& acText) noexcept;
// Authentication uses a manifest index (+1, zero means null), before server IDs are assigned.
bool EncodeInitialForm(uint32_t aRuntimeForm, const Mods& acLocalMods, GameId& aId) noexcept;
bool ResolveInitialForm(const GameId& acId, const Mods& acLocalMods, const Mods& acServerMods, GameId& aResolved) noexcept;
} // namespace ContentCompatibility
