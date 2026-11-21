#pragma once
namespace AcceleratedQuests
{
// This first scope is a build-wide contract, not a per-client preference.
inline constexpr bool kEnabled = true;
inline constexpr unsigned kMaxPending = 64;
}
