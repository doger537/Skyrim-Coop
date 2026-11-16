# Deep co-op implementation work log

**This initial log is superseded by [the current checkpoint](DEEP_COOP_CHECKPOINT_20261117.md).** Its implementation counts and pending binary-analysis notes below describe the earlier starting point.

Active branch: `fix/deep-host-coop-20261116`, based on the verified environment-resync candidate `2a1ca7ca`.

The user explicitly requested continuing implementation of all five modules, after configuring binary verification. A completed setup is a prerequisite, not completion of the gameplay request.

## Binary verification is available

The user installed Ghidra 12.0.4 with 13bm/GhidraMCP 0.2.2. Its local TCP backend listens on 127.0.0.1:8765; the configured Go stdio bridge works. The current chat's direct tool catalog has not refreshed, but actual MCP initialize/tools/list/tools/call requests through the configured bridge have succeeded. The read-only client and full local evidence are in `D:\CodexTools\SkyrimCoop-RE`.

Use `probe_mcp.py --calls <JSON path> --output <result filename> --expect-sha256 2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679 --quiet` with bundled Python 3.12.14. See local `SETUP-RU.md` for current runtime paths. This client only permits explicitly whitelisted read-only queries and checks PE format/image base/hash before and after the batch. It retains repeated calls in a `calls` array.

The first user import was Raw Binary, with incorrect virtual addressing. A separate byte-identical copy was imported correctly as PE and analyzed. Correct active program: `SkyrimSE-1.7.104.exe`; project: `D:\EGOR\skyrin\skyrim.gpr`; copy: `D:\CodexTools\SkyrimCoop-RE\targets\SkyrimSE-1.7.104.exe`. Auto analysis completed 2026-11-16 12:51:46 Moscow; the database was saved and its tab verified clean. Warnings: missing SkyrimSE.pdb, and one demangler variable at 0x141736440. Do not assume types/signatures from these warnings have been recovered. Do not publish the executable or Ghidra database.

Only a 1.7.104 database is available. `Tools/Scripts/addrlib.py -v 1.7.104`, with SKYRIM_SE_DIR set to the actual game directory, maps IDs correctly (format 5).

Verified through MCP: ID 40454 -> 0x140746e00, Actor::DropObject, body 1238 bytes / 310 instructions ending 0x1407472d5. The second argument is an output 32-bit reference handle; RDX is preserved in R12 and returned via RAX. Native code may return an invalid handle. It calls ID 19790 SetPosition (0x1402f0ef0). Existing source queues the inventory event before the native result and discards the created handle; it cannot establish a shared dropped-instance identity. Thread/lifetime and full physics behavior remain separate verification requirements.

Additional mapped IDs, not yet newly verified: 17201 GetByHandle -> 0x1402676c0; 19846 GetHandle -> 0x1402f60c0; 400312 invalid-handle global -> 0x14219e55c; 37356 ActorProcess -> 0x14067c380 (function boundaries checked).

## Implementation sequence

1. Shared world-instance identity and lifecycle; post-result drop capture; host-confirmed spawn/pickup; physics snapshots/interpolation and Havok/ragdoll integration.
2. NPC state transitions, guest conversation lease and reliable cleanup; separate persistent player position source for compass/map/world markers.
3. Host save/load hooks verified in the binary; supported world/player snapshots, storage worker and rollback barrier with a new world epoch.
4. Full host date/time/weather state plus sleep/wait voting and cancellation across cells.
5. Bidirectional journal with quest-run identity, initiator item custody and exactly-once rewards; verified quest adapters and Alternate Start compatibility.

The detailed contracts and unresolved engine questions are in HOST_AUTHORITY_DESIGN.md. Do not silently replace these requirements with existing partial relay behavior. All game API access remains on the game runner; workers only handle copied data/network/storage. Unit tests are for protocol, math and storage; game behavior still requires two-client validation.

## Source findings to preserve

- InventoryChangeEvent, RequestInventoryChanges, NotifyInventoryChanges currently have no dropped-instance identity/pose. Request has an OperationId; notify does not. Server bEnableItemDrops defaults false. Client remote drop uses DropOrPickUpObject with null location/rotation and discards the result.
- Server Campaign::State / WorldStore are in Code/persistence. SQLite commits are currently synchronous in inventory/object services. A storage worker must not access engine objects.
- SaveLoad.h contains explicitly guessed/inconsistent legacy BGSSaveLoadManager::SaveData layouts. SaveLoad.cpp has an unhooked stub. Do not use those fields for new save hooks without binary verification.
- A local CommonLib reference exists under work/engine-fixes-compat/aers-beta5-src/extern/CommonLibSSE. Its engine wrappers are references, not binary proof. Candidate AE IDs: TESDataHandler::CreateReferenceAtLocation 13723; NiAVObject::SetMotionType 77866; UpdateRigidConstraints 78103; hkpEntity::Activate 60849. Map and inspect actual 1.7.104 functions before use.
- The attached original specification names Alternate Start — Live Another Life and permits a separate own quick-start implementation if integration cannot be made safe. It explicitly does not authorize modifying third-party source files.

No new gameplay module is marked complete by this work log. The previous native candidate and its package remain unchanged until a new implementation is built and checked.

## Current code and checks (in progress)

- Actor.cpp now copies item ExtraData before the native drop, calls the original with inventory echo suppression, and queues the event after the result exists. InventoryChangeEvent has a local-only DroppedReferenceHandle; no engine pointer crosses the runner queue. ScopedInventoryOverride now bypasses replication at hook entry. DropObject and DropOrPickUpObject return their native handles for binding remote copies later.
- New encoding Structs/WorldItemState.{h,cpp} contains campaign-scoped uint64 item ID, world/host epochs, lifecycle revision, sample sequence/tick, full item properties, cell/worldspace GameIds, quantized position, WXYZ quaternion and linear/angular velocities. ReadBits returns bool; every new field read is checked. Its inventory codec is bounded to 64 enchantment effects rather than using the old unbounded Entry::Deserialize. It is not wired to transport/services yet.
- Three new engine-independent world_item_encoding tests passed: 209 assertions, including every byte truncation, round trip with enchantment/velocities, and invalid numeric/identity data. Current Catch2 is v2; use catch2/catch.hpp, not catch_test_macros.hpp.
- `xmake build --yes SkyrimTogetherClient` passed after the hook/wrapper changes (101.156 seconds). This was a static library build, not a newly packaged/deployed client. TPTests build also passed. Full-suite tests, native launcher/server builds and network integration follow the service changes.
- Toolchain: work/toolchain/xmake/xmake/xmake.exe, VSInstallDir=`D:\Новая папка\`, VCInstallDir=`D:\Новая папка\VC\`, VisualStudioVersion=17.0; use process-only env. Build.ps1 is in Tools/TrueCoop (reconfigures everything, unnecessary for each small incremental build).
- MCP physics-api-results.json now includes real decompiles for GetByHandle 17201, GetHandle 19846, CreateReferenceAtLocation 13723->0x1401bc1c0, SetMotionType 77866->0x14104cb90, hkpEntity::Activate 60849->0x140b4aff0. The decompiler text can itself be a JSON-encoded string; json.loads it before reading/exporting. Normalized local C files are decompiled-0x<address>.c.
- GetByHandle returns bool and retains the resolved reference by atomic increment at +0x28. The existing GetByHandle wrapper immediately decrements it before returning a raw pointer; a new capture/apply operation should keep a scoped strong reference until done instead of retaining a raw pointer between ticks.
- CreateReferenceAtLocation has an explicit output-handle pointer as its second ABI argument (despite the CommonLib C++ return type). Its full behavior has not yet been reviewed: only the first ~215 normalized lines have been inspected. Do not yet add a call from an assumed ABI or use its flags as guessed semantics.
- SetMotionType ABI has node/motion/recurse/force/allowActivate arguments, checks a current physics context when force=false, and dispatches through a traversal helper. A true return does not prove a body was changed; native physics locking and traversal callback still require verification.
- CommonLib hkpMotion::MotionType::kKeyframed=4; hkpEntity has motion at +0x150 (reference only). Never use these new offsets in game code without binary evidence. Current own NiAVObject is opaque after +0x30, sizeof 0x110.

Next: separate a small high-rate motion record from item creation metadata (avoid copying Inventory vectors at 20Hz), add world-epoch/revision/tombstone acceptance and interpolation, then wire host-controlled item lifecycle into inventory and object services with persistence/late join. Continue all remaining user modules; do not present the new codec or setup as their completion.
