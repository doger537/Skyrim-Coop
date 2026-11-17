#pragma once
#include <cstdint>
struct TESQuest;
struct TESObjectREFR;
struct NotifyPartyLocations;
struct GameId;
struct NiPoint3;

namespace PlayerMarkerQuest
{
TESQuest* Get() noexcept;
bool Ready() noexcept;
bool Bind(TESQuest* apQuest, uint32_t aSlot, TESObjectREFR* apReference) noexcept;
void Clear(TESQuest* apQuest, uint32_t aSlot) noexcept;
uint32_t CreateAnchor(const GameId& acCell, const GameId& acWorld, const NiPoint3& acPosition) noexcept;
bool MoveAnchor(uint32_t aHandle, const NiPoint3& acPosition) noexcept;
bool IsAnchor(const TESObjectREFR* apReference) noexcept;
void DestroyAnchor(uint32_t aHandle) noexcept;
}
