# Deep co-op implementation checkpoint

Latest continuation: the user requests all remaining modules from easiest and excludes **Alternate Start**. Implemented native party markers and cross-cell locations (commit c93d645d), then saved character identity/enrollment and a post-load session barrier. See [PLAYER_MARKERS.md](PLAYER_MARKERS.md) and [CHARACTER_IDENTITY.md](CHARACTER_IDENTITY.md). Protocol is now **0x54430008**; current core ESP SHA256 is ff52f44a06d57bded74a2a306966844845ec254d8b9ae015b30ec493ff224502. Critical tests: **94 cases / 2727 assertions**; asset checks: **6 PASS**. Fresh isolated server suites: **69 PASS** (10 rest, 11 markers, 13 dialogue, 22 general including duplicate/zero character admission, 4 campaign admission, 9 environment including normal 60-second calendar resend). These suites used a fresh DB per case to avoid reusing synthetic quest histories. All owned test server processes were stopped after exact executable-path checks. No game/Vortex/save/production DB was overwritten. Current feature code needs a committed native identity, final rebuild and packaging before handoff. Ghidra CodeBrowser/MCP remains open. Two real game clients and frame times remain untested.

Branch `fix/deep-host-coop-20261116`, based on `2a1ca7ca`; commits `f59bf388` and native candidate `2d630a5a`. Packaging documentation is in later commits. This remains an intermediate implementation of the user's five modules. A verified test ZIP is available; game/Vortex/saves have not been overwritten. The user will test with their friend later.

## Candidate handed off for later gameplay testing

- Archive: `outputs/SkyrimTrueCoop-AE-deep-coop-test-1.7.104-2d630a5a.zip`, 176337419 bytes. SHA-256 `99A226FA5719216EDD22F71950CC0AEAFF1DE3123E166D99E5FAFC7260162090`.
- Native revision `2d630a5ad7ab50efea5c277673906a44cafb39d3`, build `v1.8.4-53-g2d630a5a`, protocol `0x54430006`, Skyrim 1.7.104.0. Later documentation commits do not change Code/Libraries/modules/xmake.lua. Package-AE verifies that distinction explicitly; do not rebuild and silently reuse this archive's identity for another native revision.
- Package-AE verified hashes of 527 payload files. BUILD.json, new DEEP_COOP_TEST_RU.md and UI/index.html are present. No CommonSE launcher, game executable, world database or diagnostic/test executable is in the archive. Both players and the server need this same package.
- Source pushed; draft PR [#3](https://github.com/doger537/Skyrim-Coop/pull/3), stacked on #2. Neither PR was merged.
- Final native identity rebuilt successfully: client/launcher 18.187 s, server/DLL/runner 22.406 s, protocol probe 5.265 s. TPTests directly rerun: 87 cases / 2124 assertions passed.
- Fresh `work/deep-coop-release-2d630a5a` / port 38635 rechecked final server/probe pairing with 10 rest and 13 dialogue PASS. Earlier full wire validation of the same server feature code also passed 20 general and 9 environment scenarios, including the real 60-second calendar refresh.
- All three owned isolated server processes (22468, 25400, 14048) were stopped after checking each exact fixture executable path. Leave Ghidra open; it is the user's requested RE surface.
- User response to testing availability: **with friend later**. No two-game AI/Havok/frame-time result is available. Do not present this as completion of all five modules or resume asking for the same availability immediately.

## Implemented

- Guest dialogue leases now run end-to-end: live menu heartbeat, server sequence/epoch/range checks, renewal, bounded local movement intent, cleanup on closure/expiry/cell/ownership/party/disconnect. Uses verified native StopMoving without changing persisted life state. **NPC motion and animation still require two-client gameplay verification.** Details: [DIALOGUE_LEASE.md](DIALOGUE_LEASE.md).
- Pickup hooks corrected to their verified void/bool ABI, preserve partial-stack count after ExtraCount extraction, and avoid replay echo on player pickups. A pickup is still not a host-confirmed item transaction.
- Runner gameplay events now carry a local generation barrier; connection start, disconnect and successful campaign admission invalidate older work. Inventory/equipment batches and quest callbacks use it. Spell-cast queued events carry copied IDs and flags instead of an ActorMagicCaster pointer. A live rollback must also advance this barrier once implemented.

- Shared hour/date/elapsed-day counter, guest's future date no longer overrides the host timeline. Double elapsed days and a float-hour remainder prevent frame-rate-dependent accumulation. Minute conversion corrected; checked finite/date codec added.
- Cooperative rest: two votes within 30 seconds, then two live-menu acknowledgements. Uses the lesser duration. Timeout, menu closure, membership change or disconnect cancels. Native SleepWait callback integration and RU/EN popups are present. Actual game menus/bonuses still need two-client testing.
- Every gameplay message carries a uint64 world epoch; factories reject another epoch before decoding a payload. Epoch is durable and monotonic. Authentication bootstraps it; protocol is now `0x54430006`. Live checkpoint/rollback is not yet implemented.
- Drop hook captures the returned reference handle after the native drop. ExtraCount extraction no longer overwrites the negative inventory delta. A scoped batch preserves inventory-before-equipment ordering. ScopedReference retains a resolved reference for one operation. Generic native reference factory wrapper added.
- Bounded world-item lifecycle and small motion codecs, independent of inventory vectors at motion cadence. Epoch/revision/sequence guards, tombstones, 16-sample interpolation and 100 ms maximum extrapolation.
- Durable item codec and campaign methods LoadWorldItems/SaveWorldItem. IDs come from monotonic DB revision; CAS rejects stale writes and ordinary resurrection; pose, velocities and custom enchantments survive reopening. **Not yet an atomic inventory/item transfer or a Skyrim save rollback.**
- Native PhysicsReplication adapter: fixed queues, value-only results, no retained engine pointers, main-runner thread guard, scoped reference/root retention. Executes only just before outermost native world unlock, while this thread already owns the write lock. No custom lock or explicit wait/spin was added. 500 microsecond cooperative frame budget, 64 queue slots, two-second expiry. Currently supports one loose rigid body and refuses actors/multiple-body graphs. **No gameplay service issues requests yet, so this does not itself synchronize items.** A single native call may exceed the budget; live frame measurements are required.

## Verified

- TPTests: **87 cases / 2124 assertions passed**. Includes every truncated envelope/lease body, bounded item codecs, stable item/tombstone persistence across reopening, stale CAS updates, date boundaries, frame-rate-independent prediction and deadline wrap/expiry/capacity.
- Native launcher/client, STServer.dll, server runner and protocol probe all built. Initial dialogue build logs: `work/deep-coop-dialogue-client-build.log` (113.281 s), `deep-coop-dialogue-server-build.log` (59.031 s), `deep-coop-dialogue-probe-build.log` (5.109 s). Final candidate logs use the `deep-coop-release-` prefix; see candidate block above. Not deployed to the game.
- UI TypeScript and production build passed; log `work/deep-coop-ui-build.log`, hash `6cc28e1e44759ea9`. Direct pinned node_modules CLI used; globally bundled pnpm tried an incompatible migration and was not used to upgrade dependencies.
- Isolated `work/deep-coop-sleep-20261116`, port 38633: cooperative-rest probe **10 PASS**, default protocol probe **20 PASS**, environment probe **9 PASS**, including an actual 60-second calendar resend. These validate the wire/server path, not native gameplay. Fixture's copied DLL predates the latest item-persistence, physics adapter and hour-remainder changes. Recopy latest runner/DLL into a fresh fixture for subsequent validation.
- Fixture server PID was 22468. Verify exact executable path before stopping this test process; never stop a user's game/server by name.
- Fresh fixture `work/deep-coop-dialogue-20261117-6ed747c1`, port 38634, PID25400, includes the latest item persistence/physics/calendar/dialogue code. Cooperative-rest probe **10 PASS**, dialogue probe **13 PASS**, default probe **20 PASS**, environment probe **9 PASS** including a normal 60-second refresh. Verify exact executable path before stopping it.
- Source and checked test package are now published/available as described above. Packaging guide uses BUILD.json for the actual identity instead of hard-coding an obsolete archive version.

## Ghidra recovery and evidence

Correct program `SkyrimSE-1.7.104.exe`, project `D:\EGOR\skyrin\skyrim.gpr`, image base `0x140000000`, SHA-256 `2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`. Only this runtime database is available. Other two entries include the old incorrect raw import.

User reopened Ghidra after it was closed. Project tool association was missing. Launch the first existing Untitled tool; Ctrl+O; Shift+Tab from the filter to the tree, Down from the project root to the first PE file, Enter. This successfully opens the saved analysis and its MCP plugin; **no reanalysis/reimport needed**. Default-tool import was empty and cancelled. Leave the browser open.

Sky (@oai/sky through node_repl) was used for UI. The tool window was partially off-screen; coordinate clicks in its cropped screenshot did not target the intended file. Keyboard navigation worked. After user input/interruption, refresh window selection and state. Current tool handle/title must be rediscovered, not reconstructed.

Ghidra 12.0.4 / 13bm GhidraMCP 0.2.2, localhost:8765. Direct `mcp__ghidra` tools became available in the resumed chat and work with the correct program. The earlier MCP stdio bridge `D:\CodexTools\SkyrimCoop-RE\probe_mcp.py` also works and checks PE/hash before and after. MCP text may be JSON encoded twice; normalize before reading xref objects. Never publish executable/database.

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
2. Verified full ragdoll graph, physical grab intents, NPC animation/furniture/navmesh. Guest-dialogue lease transport/native intent is implemented but needs actual gameplay verification. Current physics adapter deliberately rejects actors; it does not solve ragdoll bugs.
3. Player compass/map/world markers: native quest-target integration and distant model-free anchors are implemented; rendering, door routing and frame times need gameplay verification. Saved character ID, durable server enrollment and duplicate-session rejection are implemented. Real .ess identity restoration still needs gameplay proof.
4. Native paired save/load checkpoints, world/player snapshots and a live rollback/epoch barrier. The new native load-completion guard discards old pending events/authentication and closes the old connection; it is not paired rollback or a pre-load hook.
5. Quest-run identity, initiator-only item custody, exactly-once rewards, complete shared journal. **Do not modify Alternate Start:** user excluded it in the latest request.

Continue all five; do not present the codec, queue or setup as complete gameplay. All engine API calls remain on engine/runner thread; workers carry copied data only. Gameplay tests need user participation; unit tests stay restricted to critical protocol/math/storage.

Build: process-only VSInstallDir=`D:\Новая папка\`, VCInstallDir=`D:\Новая папка\VC\`, VisualStudioVersion=17.0. Xmake `work/toolchain/xmake/xmake/xmake.exe`; use `-j 4` alongside Ghidra. Regenerate shared build identity after committing before packaging any candidate. Preserve upstream attribution and the user's original specification.
