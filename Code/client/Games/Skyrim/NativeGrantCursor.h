#pragma once
#include <TiltedCore/Stl.hpp>

// All calls use the existing admitted runner. The nonconstant globals are
// serialized/restored by Skyrim itself together with the player's inventory.
namespace NativeGrantCursor
{
enum class Result { Unbound, Bound, Invalid };
Result Read(TiltedPhoques::String& aWorld, uint64_t& aRevision) noexcept;
bool Bind(const TiltedPhoques::String& acWorld) noexcept;
bool Advance(const TiltedPhoques::String& acWorld, uint64_t aExpected, uint64_t aNext) noexcept;
bool IsWriting() noexcept;
}
