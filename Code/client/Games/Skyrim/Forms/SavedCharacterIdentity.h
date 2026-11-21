#pragma once
#include <Structs/CharacterIdentity.h>

namespace SavedCharacterIdentity
{
enum class Result { Found, Unassigned, Invalid };
// Engine/runner thread only. Never retain the returned native forms.
Result Read(CharacterIdentity& aIdentity) noexcept;
bool Commit(const CharacterIdentity& acIdentity) noexcept;
}
