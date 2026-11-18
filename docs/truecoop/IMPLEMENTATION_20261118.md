# Implementation checkpoint — 2026-11-18

Current continuation: the user approved an accelerated host-led scope and shared
item effects instead of classifying every key/reward. An experimental producer is
now connected for standard base-form AddItem and RemoveItem (no destination).
It uses permission before native execution, actual count readback, immutable
signed Shared receipts, guest duplicate suppression and both-recipient cursor
acknowledgement before VM release. Protocol is **0x5443000D**, SQLite **schema 5**.
Details and explicit unsupported cases: [ACCELERATED_QUESTS_RU.md](ACCELERATED_QUESTS_RU.md).
The earlier notes below describe the classified route/foundations; they do not
supersede this narrower newly enabled producer. No tests or game launch occurred.

Latest user order: physics and NPCs first, then finish quests and make the ZIP; do not change Alternate Start. All tests remain deferred. No automatic/gameplay tests, game launches or installed-game/save/profile modifications were performed after that instruction.

Implemented in this checkpoint:

- Durable quest grant plans, both recipient indexes in one transaction, permanent quest initiator, role-specific projection, bounded revision paging, corruption checks and rollback with world checkpoints. Current SQLite schema 4 also fences the new item-receipt ledger from older servers.
- Native AddItem provenance observer and bounded latent-functor gate for the exact 1.7.104 binary checked through Ghidra MCP. It copies Quest/Alias/TopicInfo/Scene source metadata across both actor/ref inventory hooks. The gate can defer original execution and VM resume through the verified native requeue mechanism, with save barriers and native destruction. No semantic policy is configured, so ordinary rewards still pass through. Details: `NATIVE_QUEST_GATE.md`.
- Paired-file admission proof: the existing verified load hook records a bounded owned filename; an acknowledged paired save records its committed baseline. A token-bearing reconnect must name the correct world/checkpoint/character. ESS and optional SKSE hashes are calculated in the existing file worker, then matched against the server's immutable checkpoint manifest. Ordinary saves cannot pass merely by inheriting checkpoint globals. Failed/unknown load provenance is refused, not guessed. Protocol is now `0x5443000C`, including grant inbox and ragdoll transport.
- Read-only grant inbox transport, bounded receipt decoding, permanent-recipient checks, per-tick inventory application/readback and a native saved revision cursor. Our separate `TrueCoopWorldState.esp` preserves thirteen nonconstant quest globals and now adds the script-free item registry/ID/cursor records, without overrides. The client links pure codecs rather than the SQLite campaign service. Pending/interrupted native operations block saves; actual load resets the barrier by generation. Keys needing aliases and dynamic inline enchantments are refused without the needed native proof. The real-quest producer/broker is still absent, so this is a delivery path, not enabled automatic vanilla reward synchronization. Details: `QUEST_GRANT_DELIVERY.md`.
- Host approval of guest quest lifecycle/stage updates, shared preview/commit validation, guarded queues, native observation on runner ticks and preservation of the guest initiator. Pending approvals block common save/load. Details: `HOST_QUEST_APPROVAL.md`.
- Connection callback routing by native handle with delivery on each client's Update thread, rather than dispatch to whichever client happened to pump the global callbacks. The pinned TiltedConnect submodule remains intact; local overlay includes its original notice.
- NPC pose, real furniture binding, bounded get-up recovery, keyed multi-body ragdoll replication and guest dragging through the simulation owner. Native physics queues and animation catch-up have separate shared frame budgets and ownership/load fences. Protocol is now `0x5443000C`. These are implemented paths, not verified gameplay fixes. Details: `PHYSICS_NPC_20261118.md`.
- Atomic world-item lifecycle/inventory receipts, durable session/operation deduplication, bounded recipient paging and bidirectional integrity checks. The script-free native registry reserves 64 object aliases and a separate saved item cursor while preserving all thirteen quest-grant globals. The persistence and registry APIs are not connected to gameplay yet. Details: `WORLD_ITEM_RECEIPTS.md`.
- Error review fixes: default GameId initialization, checked quest packet reads, proper equality in quest-name lookup, avoidance of a repeated native stop, and a typed null for the optional .NET file replacement backup argument in the deferred diagnostic helper.

Prior checks (before the latest no-tests instruction): 112 storage/protocol cases / 3982 assertions and three paired-checkpoint protocol recovery runs passed on the earlier source state. They do **not** validate the new file-admission or host-approval changes. Production client/server compilation is repeated on the final checkpoint; no tests are rerun.

Still incomplete:

1. Automatic native quest custody/reward broker: semantic source classification, verified adapters, server permission/commit from collected actual effects, both-recipient completion and VM release, alias/direct alias-fill coverage, repeatable occurrence IDs and XP/spell rewards. Gate, inbox and saved cursor are implemented supporting layers; their presence does not prove end-to-end correctness or coverage of real quests.
2. Full end-to-end dropped-object lifecycle and mapping of logical IDs to native references across native saves. Existing persistent WorldItemState, interpolation and one-rigid-body capture/apply API are supporting pieces, not a completed dropped-item service.
3. Physics continuation now implements the runtime ragdoll path (up to 32 keyed bodies), guarded guest drag input, managed NPC pose playback and verified stable furniture transitions. The dropped-object service, exact rig checkpoint capture, unsupported models, scene bindings and full navmesh/animation-graph state remain incomplete. New MCP evidence and limits: `PHYSICS_NPC_20261118.md`.
4. Two real clients validating paired ESS reload, time/weather, sleep, NPC dialogue, player markers and frame-time cost. User deferred these checks.

The existing Ghidra CodeBrowser was opened directly on `skyrim:/SkyrimSE-1.7.104.exe`; MCP is responding with the correct PE, 176587 functions and the expected executable SHA-256. Actual MCP checks verified the complete AddItemFunctor vtable, destructor, save/load/type methods, scheduler call convention/requeue branch and native global serialization. The project was preserved; no reimport or Skyrim launch was required. Evidence and assumptions are listed in `NATIVE_QUEST_GATE.md`. This checkpoint does not claim all five modules complete.
