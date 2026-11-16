# Deep co-op implementation checkpoint

Branch `fix/deep-host-coop-20261116`, based on `2a1ca7ca`. This remains an intermediate implementation of the user's five modules. There is no new installable ZIP, and game/Vortex/saves have not been overwritten.

## Implemented

- Shared hour/date/elapsed-day counter, guest's future date no longer overrides the host timeline. Double elapsed days and a float-hour remainder prevent frame-rate-dependent accumulation. Minute conversion corrected; checked finite/date codec added.
- Cooperative rest: two votes within 30 seconds, then two live-menu acknowledgements. Uses the lesser duration. Timeout, menu closure, membership change or disconnect cancels. Native SleepWait callback integration and RU/EN popups are present. Actual game menus/bonuses still need two-client testing.
- Every gameplay message carries a uint64 world epoch; factories reject another epoch before decoding a payload. Epoch is durable and monotonic. Authentication bootstraps it; protocol is now `0x54430005`. Live checkpoint/rollback is not yet implemented.
- Drop hook captures the returned reference handle after the native drop. ExtraCount extraction no longer overwrites the negative inventory delta. A scoped batch preserves inventory-before-equipment ordering. ScopedReference retains a resolved reference for one operation. Generic native reference factory wrapper added.
- Bounded world-item lifecycle and small motion codecs, independent of inventory vectors at motion cadence. Epoch/revision/sequence guards, tombstones, 16-sample interpolation and 100 ms maximum extrapolation.
- Durable item codec and campaign methods LoadWorldItems/SaveWorldItem. IDs come from monotonic DB revision; CAS rejects stale writes and ordinary resurrection; pose, velocities and custom enchantments survive reopening. **Not yet an atomic inventory/item transfer or a Skyrim save rollback.**
- Native PhysicsReplication adapter: fixed queues, value-only results, no retained engine pointers, main-runner thread guard, scoped reference/root retention. Executes only just before outermost native world unlock, while this thread already owns the write lock. No custom lock or explicit wait/spin was added. 500 microsecond cooperative frame budget, 64 queue slots, two-second expiry. Currently supports one loose rigid body and refuses actors/multiple-body graphs. **No gameplay service issues requests yet, so this does not itself synchronize items.** A single native call may exceed the budget; live frame measurements are required.

## Verified

- TPTests: **85 cases / 2024 assertions passed**. Includes every truncated envelope, bounded item codecs, stable item/tombstone persistence across reopening, stale CAS updates, date boundaries and frame-rate-independent prediction.
- Native launcher/client, STServer.dll, server runner and protocol probe all built. Latest native build log `work/deep-coop-native-build.log`: 57.594 s. They are not deployed to the game.
- UI TypeScript and production build passed; log `work/deep-coop-ui-build.log`, hash `6cc28e1e44759ea9`. Direct pinned node_modules CLI used; globally bundled pnpm tried an incompatible migration and was not used to upgrade dependencies.
- Isolated `work/deep-coop-sleep-20261116`, port 38633: cooperative-rest probe **10 PASS**, default protocol probe **20 PASS**, environment probe **9 PASS**, including an actual 60-second calendar resend. These validate the wire/server path, not native gameplay. Fixture's copied DLL predates the latest item-persistence, physics adapter and hour-remainder changes. Recopy latest runner/DLL into a fresh fixture for subsequent validation.
- Fixture server PID was 22468. Verify exact executable path before stopping this test process; never stop a user's game/server by name.
- Latest working tree has no whitespace errors. No new commits/push/package yet at this checkpoint.

## Ghidra recovery and evidence

Correct program `SkyrimSE-1.7.104.exe`, project `D:\EGOR\skyrin\skyrim.gpr`, image base `0x140000000`, SHA-256 `2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`. Only this runtime database is available. Other two entries include the old incorrect raw import.

User reopened Ghidra after it was closed. Project tool association was missing. Launch the first existing Untitled tool; Ctrl+O; Shift+Tab from the filter to the tree, Down from the project root to the first PE file, Enter. This successfully opens the saved analysis and its MCP plugin; **no reanalysis/reimport needed**. Default-tool import was empty and cancelled. Leave the browser open.

Sky (@oai/sky through node_repl) was used for UI. The tool window was partially off-screen; coordinate clicks in its cropped screenshot did not target the intended file. Keyboard navigation worked. After user input/interruption, refresh window selection and state. Current tool handle/title must be rediscovered, not reconstructed.

Ghidra 12.0.4 / 13bm GhidraMCP 0.2.2, localhost:8765. Direct ghidra tools remain absent from this chat catalog. Actual MCP stdio bridge calls succeed using `D:\CodexTools\SkyrimCoop-RE\probe_mcp.py`; it checks PE/hash before and after and whitelists read-only tools. Search-bytes/data listing were added to that whitelist. MCP text may be JSON encoded twice; normalize before reading xref objects. Never publish executable/database.

Bundled Python: `C:\Program Files\WindowsApps\OpenAI.CodexPrimaryRuntime.v26-1112-737-0_26.1112.737.0_x64__3k8sg7r9htsxt\dependencies\python\python.exe`.

Evidence files in that local RE directory:

- physics-method-results, collision-method-results, deep-physics-results: retained handles, pose getter/setter ABI, native lock reentrancy, collision-body and world association.
- physics-transform-slots-results and physics-transform-methods-results: collision virtual methods; native body velocity fields 0x230/0x240; XYZW quaternion layout.
- physics-casts-results and physics-casts-methods-results: virtual AsNode 03, AsBhkNiCollisionObject 12, AsBhkRigidBody 15, default null and node self-return implementations.
- physics-lock-scale-results: exact scale bytes matched through MCP at native operand addresses. ID188105 => 0x1417fde5c => 1E166A3C; inverse ID187407 => 0x1417f423c => 85FB8B42. Unmark ID77929 => 0x14104fd00; its disassembly adds 0xC598 then jumps to BSReadWriteLock::UnlockWrite ID68240. LockForWrite ID68234 is reentrant for owning thread and otherwise spins; new adapter never acquires it from an unowned state.
- sleep-menu-results and sleep-start-results: native callback ID524025 at 0x1409752a0, ProcessMessage ID52486 at 0x140974e00; handler at FxDelegateArgs+0x18, args+0x28, numeric value+0x10; menu isSleeping0x50/isActive0x51. Existing clock hook ID36291 => 0x140645d60 also intercepts native rest time advance.
- deep-gameplay-results: ExtraDataList::SetCount ID11617; save/load request wrappers IDs35727/35728 examined but not installed as checkpoint hooks. Legacy SaveLoad.h layouts remain unsuitable.

Other verified IDs: drop40454, retained handle lookup17201, generic reference factory13723 (full body reviewed), SetMotionType77866, getters78078/78079 (virtual33/34), pose setter78082 (virtual37), velocities78089/78090. Current new hooks are gated to 1.7.104.

## Remaining work

1. Host-confirmed drop/pickup service with stable binding, pre-pickup interception, replay guards and atomic inventory/item transaction. Do not merely enable old bEnableItemDrops relay. Connect native capture/kinematic playback, including missing 3D, cell transitions, expiry and disconnect cleanup.
2. Verified full ragdoll graph, physical grab intents, NPC animation/furniture/navmesh and guest-dialogue leases. Current physics adapter deliberately rejects actors; it does not solve those bugs.
3. Player compass/map/world markers with persistent identity.
4. Native paired save/load checkpoints, world/player snapshots and a live rollback/epoch barrier.
5. Quest-run identity, initiator-only item custody, exactly-once rewards, complete shared journal, Alternate Start integration.

Continue all five; do not present the codec, queue or setup as complete gameplay. All engine API calls remain on engine/runner thread; workers carry copied data only. Gameplay tests need user participation; unit tests stay restricted to critical protocol/math/storage.

Build: process-only VSInstallDir=`D:\Новая папка\`, VCInstallDir=`D:\Новая папка\VC\`, VisualStudioVersion=17.0. Xmake `work/toolchain/xmake/xmake/xmake.exe`; use `-j 4` alongside Ghidra. Regenerate shared build identity after committing before packaging any candidate. Preserve upstream attribution and the user's original specification.
