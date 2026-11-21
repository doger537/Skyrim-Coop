# Guest conversations: implementation and limits

This candidate forwards a guest's live dialogue-menu intent to the client that currently simulates the NPC. It is an incremental change on the existing ownership model, not a conversion of every NPC to host-only simulation.

The client polls the menu's current speaker every 100 ms and sends a heartbeat every 750 ms. Server admission requires two ready party participants, an assigned living NPC, its current ownership epoch, matching party/range and a distance no greater than 1024 game units. Sequence and timestamp checks reject replays and old requests. One player can hold one conversation; a second speaker cannot steal it. Each lease lasts at most three seconds and is released on menu closure, cell departure, ownership change, party change, death, disconnect or missing heartbeats. Messages are reliable and carry the world epoch.

The owner publishes a numeric FormID and a local deadline into an eight-slot lock-free table. The existing native actor movement hook checks that table without waiting. It calls the engine's StopMoving for a live, locally simulated, non-combat NPC, then executes the original movement function. Collision, controller and animation processing remain native. No new position warp, DisableAI or persisted life-state override is used. Ordinary dialogue with the local player is left to the engine. Actor combat and other scripted motion may override this intent; standing still and animation quality need two-client game verification.

The table, server lease component and client lease map are transient. They are absent from campaign records and Skyrim saved state. Local deadlines use GetTickCount64 rather than a server clock that may be rebased. The pure fixed-size table handles 32-bit timer wrap and refuses overflow. It does not retain an engine pointer.

## Engine verification

Only the 1.7.104 database is available. Ghidra MCP reports program `SkyrimSE-1.7.104.exe`, PE/x86-64, the previously checked SHA-256 `2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`.

- ID37817 -> `1406a6050`: decompilation and disassembly inspected through MCP. Actor in RCX, float delta preserved from XMM1 to XMM6 and passed to movement-agent stop `140790400`; the function clears the movement graph boolean through `140553560`. It does not call the life-state setter.
- Existing movement hook ID37356 -> `14067c380`: MCP decompilation inspected, including movement-controller access, position integration and native collision/movement callbacks. The new code continues into this original function.
- Papyrus SetRestrained callback `140a02b20` -> `140687b40` -> life-state setter `140693200`. MCP confirms state 6 is stored in Actor+0xC8 bits 21..24 and change flag 0x400 is recorded. This API was therefore rejected for a transient conversation.
- Actor InitiateDialogue virtual D8 (`14191bc08` -> `14068ed00`) was inspected, and the vtable pointer bytes were matched through MCP search_bytes. It creates a shared package and applies it to both participants. It was rejected here because applying that package to a remotely replayed player needs additional verification.
- The existing MenuTopicManager speaker handle at 0x68 and menu-open flag at 0xB1 are reused. A ScopedReference resolves and retains the handle for the runner poll only.
- Pickup ID40533 -> `14074c560` (Player) and ID37521 -> `14068b0e0` (Actor): actual void return and bool fifth argument verified through MCP. Hooks now match those ABIs and preserve requested partial-stack count after extracting ExtraCount. Pickup acceptance/arbitration is still pending; an emitted event is not proof that native pickup succeeded.

Local RE evidence lives under `D:\CodexTools\SkyrimCoop-RE` (pickup-abi-results, dialogue-native-methods-results, dialogue-hold-implementation-results). Further direct MCP calls inspected the functions above. No executable or database is included in the repository.

## Verification

TPTests: 87 cases / 2124 assertions passed, including checked lease codecs, every truncated body, stale world envelopes, table capacity, expiry and timer wrap. Native launcher, server/DLL and protocol probe compile. The new `--verify-dialogue` isolated probe passes 13 wire/server scenarios, including renewal, explicit release, competing speakers, replays, stale ownership, timeout, cell change and disconnect. It does not execute Skyrim AI or measure frame times.

The runner also invalidates delayed gameplay events at connection start, disconnect and admission to a playable campaign. Inventory/equipment batches and delayed quest callbacks use that barrier. Spell-cast events now copy numeric actor/source/dual-cast data rather than retaining an ActorMagicCaster pointer. A live save rollback must still explicitly advance the barrier and restore paired native state; that module is not complete.
