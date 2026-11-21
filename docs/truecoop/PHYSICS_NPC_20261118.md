# NPC poses, furniture and ragdoll replication — 18 November 2026

Update 19 November: a bounded ordinary-drop/pickup service now calls the native
registry and single-body physics adapter. The preparation/remaining-work section
below is historical; current limits are in `ACCELERATED_BUILD_20261119_RU.md`.

The user requested physics/NPC work before returning to quests and packaging.
All tests remain deferred. This document describes implemented paths and their
limits, not observed gameplay results. No Skyrim process was launched and no
installed files, profiles or saves were changed.

## Implemented runtime path

- Movement snapshots now contain a checked discrete ActorPoseState: ragdoll,
  native sit/sleep, knock/life mode, static furniture GameId and marker. Ownership
  epochs and the existing owner playback clock fence/schedule the state.
- Interpolation stops driving a standing character controller or root rotation
  while the owner/local actor is in a physics/furniture/get-up mode. Ragdoll
  animation variables are not continuously overwritten by ordinary locomotion.
- Animation replay preserves native life, knock, seat and reanimation bits on
  1.7.104. These belong to native lifecycle transitions, not an older network
  animation. Catch-up processes at most four due actions per actor, with a soft
  300 microsecond per-actor budget, plus a shared 1000 microsecond/16-action
  frame budget. Actor traversal rotates between frames rather than starving
  the same tail of the view. Incoming actions are ordered; a 1024-entry overflow
  fails the session instead of allocating an unbounded broken-pose backlog.
- Stable sitting/sleeping uses the native transition API with a real furniture
  reference and NIF marker. It validates type, loaded model, world/interior,
  marker existence and reservation. It does not evict another occupant, patch
  private handles, fake an occupied seat, or reset/recycle the actor. A changed
  seat goes through the native exit to release the old reservation. Combat,
  scenes, ragdoll and transitional states are deferred. One furniture attempt
  is admitted per runner frame; genuine failure retries after 500 ms, while a
  budget throttle does not postpone all other NPCs and starve them.
- A living remote actor still knocked down after at least 300 ms of **new owner
  observations** of normal standing may request the native get-up package. It
  respects local life/seat, scene, graph/process readiness and active grabbing;
  an existing get-up state is not restarted. It shares the one-posture-attempt
  frame budget and retries at most once per second. No knock-bit clear, actor
  reset, respawn or destructive ragdoll teardown is used for this recovery.
- Ragdoll capture/application traverses the currently loaded collision graph,
  with a 512-node/32-body bound and sorted unique hashes of node-name paths.
  The receiver checks every body, key, world, mode, cell, actor incarnation and
  ownership epoch before the first write. Mismatches are refused as a whole.
  Transforms and linear/angular velocities use verified native APIs; body
  motion/constraint setup is preserved. There is no copy of opaque Havok memory.
- A dedicated service captures up to 10 Hz, relays only the current simulation
  owner's state, interpolates four samples and applies at most about 33 Hz.
  Quaternion interpolation follows the short hemisphere. Prediction is bounded
  to 100 ms, 256 units and one radian; after that the native solver continues
  rather than repeatedly pinning the body to an obsolete sample.
- Physics work runs only at an existing outermost native world-write unlock on
  the admitted runner. No new lock acquisition/spin or blocking network wait is
  added. Rig queues/results are separately bounded to eight, jobs expire after
  150 ms, and the existing soft 500 microsecond frame budget is shared. Captured
  timestamps describe when capture actually ran, not when it was queued.
- Guest dragging is a distinct input path. Only the currently grabbed native
  actor can supply a guest capture. The server checks party, owner epoch,
  canonical graph/cell, range, rate, bounded displacement/velocities and a
  500 ms interactor lease. It forwards the input only to the simulation owner.
  That owner applies it and publishes its next canonical capture. Guest poses
  are never broadcast as authority. Native owner/guest simultaneous grabbing
  does not force the owner's own grip out of the way.
- Owner-side drag input rejects an older source tick; input admission rotates
  over at most 24 actors per pass, like capture/application. Thread migration
  disables native posture/physics admission rather than using the wrong thread.
- Native load, common-checkpoint preparation and disconnect invalidate pending
  physics jobs. An already queued job rechecks generation, ECS incarnation,
  epoch, form, cell, remote/local role and current native ragdoll/grab state.
  Delayed packets and jobs cannot blindly write a reused actor handle.

Protocol is now `0x5443000C`. Server/client binaries must be built and updated
together. The simulation owner is selected by the authoritative server under
the existing actor ownership system; this can delegate simulation when the
party leader has not loaded that NPC's location. It is not a new standalone
server capable of simulating all unloaded Creation Engine cells.

## Ghidra MCP evidence

The existing CodeBrowser contains the correct SkyrimSE-1.7.104.exe PE (176587
functions), SHA-256
`2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`.
No 1.6.1170 database is available. New engine behavior is guarded to 1.7.104.

| Address / ID | Verified behavior |
|---|---|
| `140A03C00`, `140A01200`, `140A01260` / 54711, 54715 callbacks | Native GetSitState/GetSleepState registration, Actor+C8 and mask 3C000. Undefined callback functions were checked with exact unique byte matches through MCP rather than presented as decompiled. |
| `140687C20` / 37491 | Actual native ragdoll predicate, including life 1E00000, knock E000000 and Actor+CC bit 4. |
| `14071FEC0` / 39798 | Occupied furniture handle at middle+208 and marker +2E8, native release/reset path. Its actor argument is present; the one-argument common-library declaration is insufficient for this binary. |
| `1407246A0` / 39912 | Native sit/sleep transition: process, actor, state, pointer to copied handle, marker; reservations/controller/animation and final native state update. |
| `1402FF270` / 20159 | Respect an existing marker occupant/reservation; guarded native used-marker map. |
| `14026EB30` / 17408 | Bounds-check the actual NIF furniture-marker array before returning marker data. |
| `1402FFA60`, constructors `14026DD40`/`14026DF70` | REFR+40 base form and furniture type 28h. |
| `140EE23A0`, `140EE0A40` | Scene children at NiNode+118/+122 and node name at +10. |
| `1410812B0`, `1410813A0`, `1410815C0` | bhkRigidBodyT world/node transform compensation in the same virtual getter/setter slots; the full setter assembly was also checked. |
| `140751870`, `140752630` | Native grab and grab setter/handle resolution. Player+8D8 is the handle on this runtime; +8D0 is the spring-array count. |
| `140691370` / 37569, Actor/Character virtual DE | Native get-up package creation and readiness checks. MCP also matched its pointer at Actor vtable 14191BC38. |
| `14067D3A0` / 37359 | Destructive B0 ragdoll recovery/reset path inspected and deliberately not called by the soft posture recovery. |
| Earlier physics MCP evidence | World wrapper/lock and release, rigid-body/world fields, collision casts, transform slots, scale globals and velocity setters used by the existing adapter. |

Evidence stays outside the repository:
`D:/CodexTools/SkyrimCoop-RE/physics-npc-furniture-20261118-mcp.json` and the
`physics-npc-supplement-20261118-mcp.json` evidence file. Neither the EXE nor
decompiler dumps are shipped.

## Dropped-object preparation and remaining work

`TrueCoopWorldState.esp` now additionally reserves a script-free quest with 64
optional non-quest-object aliases, four 16-bit ID globals per alias and a separate
item-receipt cursor. Existing quest-grant globals retain their IDs and format.
The generator extends only our exact unpublished 13-global predecessor, refuses
unknown bytes, and never modifies an installed plugin.

`WorldItemRegistry` wraps the previously MCP-verified native ForceRefTo/Clear and
nonconstant-global serialization. Reads are slot-based for incremental restore;
writes use native alias APIs and readback. Interrupted writes block saves until
actual load. **The alias registry is prepared but not called by a dropped-item
service yet.** No raw temporary FormID is being declared a persistent network ID.

The persistence layer now commits object lifecycle + one inventory effect +
recipient inbox + session/operation index atomically. Repeated claims cannot
mint a second committed effect. A separate saved cursor is reserved for items;
SQLite schema 4 refuses older servers that would ignore this ledger. This API
still needs gameplay/network callers. Details: `WORLD_ITEM_RECEIPTS.md`.

Still required before claiming the full requested physics module:

1. Connect the atomic drop/pickup ledger and native saved registry to real
   stream/session correlation, a hooked pickup claim, native creation/metadata,
   saved mapping restore,
   object motion/interaction and lifecycle delivery. The old item-drop setting
   remains off; flipping it on is not an implementation of this pipeline.
2. Exact common-checkpoint coordination of canonical rig state before native
   saves; current paired saves still store each client's native ESS state.
3. Models larger than 32 bodies, unsupported collision types, arbitrary body
   geometry/constraint compatibility and arbitrary mod metadata.
4. Full navmesh/AI package and animation-graph serialization. Standing movement
   still follows the owner's positions through the existing controller path.
   Scene furniture and missing models/markers deliberately defer to native
   scene/lifecycle handling rather than forcing a pose.
5. Actual two-client checks of dragging, furniture changes, get-up, death,
   ownership handoff, save/load, frame time and interaction with other hooks.

The budget checks do not preempt an individual native call. Native locks,
scene traversal, graph/controller transitions and saves can still cost time.
Compilation and MCP inspection do not prove zero microstutter or eliminate
every T-pose/model bug. Quest producer/broker completion and the new ZIP remain
later steps in the user's requested order. Alternate Start was not changed.
