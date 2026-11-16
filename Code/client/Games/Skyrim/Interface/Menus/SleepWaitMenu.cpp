#include "SleepWaitMenu.h"
#include <Interface/UI.h>
#include <World.h>
#include <Events/CoopSleepIntentEvent.h>
#include <atomic>
#include <cmath>

namespace
{
std::atomic<uint64_t> s_menuSession{};
using TStart = void(const FxDelegateArgs*);
using TProcess = UI_MESSAGE_RESULTS(SleepWaitMenu*, UIMessage*);
TStart* RealStart{};
TProcess* RealProcess{};

SleepWaitMenu* CurrentMenu() noexcept
{
    auto* ui = UI::Get();
    return ui && ui->GetMenuOpen("Sleep/Wait Menu") ? static_cast<SleepWaitMenu*>(ui->FindMenuByName("Sleep/Wait Menu")) : nullptr;
}

void HookStart(const FxDelegateArgs* apArgs)
{
    auto& world = World::Get();
    if (!world.GetTransport().IsConnected())
    {
        RealStart(apArgs);
        return;
    }
    // The verified native callback reads handler +0x18, argument pointer +0x28
    // and its numeric value +0x10. Copy values only, never retain its arguments.
    if (!apArgs || !apArgs->pHandler || !apArgs->pArgs || apArgs->numArgs != 1 || apArgs->pArgs[0].GetType() != GFxValue::kNumber)
        return;
    const double hours = apArgs->pArgs[0].value.number;
    if (!std::isfinite(hours) || hours < 1 || hours > 24 || std::floor(hours) != hours)
        return;
    auto* menu = static_cast<SleepWaitMenu*>(apArgs->pHandler);
    if (menu->isActive)
        return;
    world.GetRunner().Trigger(CoopSleepIntentEvent{SleepWaitMenu::Session(), world.GetTransport().GetConnectionGeneration(),
                                                 static_cast<uint8_t>(hours), menu->isSleeping});
}

UI_MESSAGE_RESULTS HookProcess(SleepWaitMenu* apMenu, UIMessage* apMessage)
{
    if (apMessage && (apMessage->eType == UIMessage::kShow || apMessage->eType == UIMessage::kHide || apMessage->eType == UIMessage::kForceHide))
        ++s_menuSession;
    return RealProcess(apMenu, apMessage);
}
} // namespace

uint64_t SleepWaitMenu::Session() noexcept { return s_menuSession.load(); }

bool SleepWaitMenu::IsPending(uint64_t aSession, bool aSleeping) noexcept
{
    const auto* menu = CurrentMenu();
    return menu && Session() == aSession && menu->isSleeping == aSleeping && !menu->isActive;
}

bool SleepWaitMenu::BeginApproved(uint64_t aSession, uint8_t aHours, bool aSleeping) noexcept
{
    if (!RealStart || aHours < 1 || aHours > 24 || !IsPending(aSession, aSleeping))
        return false;
    GFxValue hours{};
    hours.type = GFxValue::kNumber;
    hours.value.number = aHours;
    FxDelegateArgs args{};
    args.pHandler = CurrentMenu();
    args.pArgs = &hours;
    args.numArgs = 1;
    // Calls the same native rest callback with fresh stack-owned values. This
    // preserves the game's rest/bonus lifecycle without advancing local time.
    RealStart(&args);
    return true;
}

static TiltedPhoques::Initializer s_sleepWaitHooks([]()
{
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0")
    {
        spdlog::warn("Cooperative rest hooks require binary verification for this runtime");
        return;
    }
    // MCP: sleep-start-results.json / sleep-menu-results.json, PE 1.7.104.
    POINTER_SKYRIMSE(TStart, start, 524025);
    POINTER_SKYRIMSE(TProcess, process, 52486);
    RealStart = start.Get();
    RealProcess = process.Get();
    TP_HOOK(&RealStart, HookStart);
    TP_HOOK(&RealProcess, HookProcess);
});
