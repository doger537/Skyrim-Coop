# Placed physics and paired-save alignment — 2026-11-19

This experimental 1.7.104 implementation discovers movable physics on ordinary
placed references in the clients' attached cells. It no longer limits live
physics replication to managed inventory drops and actors. It also adds a
physical alignment barrier before paired native saves and after paired loads.
Compilation and binary inspection are validation of the implementation, not
evidence of successful two-client gameplay or measured frame times. Automated
tests and Skyrim launches were explicitly deferred by the user.

## Live replication

`PhysicalCellScan` visits at most 64 table entries and retains at most 16
references per call. It tries the cell table lock once, deferring if busy.
Retained entries become native handles after releasing the lock. No borrowed
engine pointer survives a tick or crosses into a worker.

The client probes native movable body graphs instead of selecting only a short
list of base form types. Fixed bodies and references without movable collision
are excluded. Each supported graph has at most 32 unique node-path keys, with a
512-node traversal bound. Unsupported/changed graphs are never partially written.

The server selects a simulation owner from reported loaded-cell coverage. It
prefers the host; a guest can receive a delegated lease where the host has not
loaded the cell. Owner changes receive monotonically increasing epochs and an
initial pose that the new owner must apply before publishing. Guest grab input
is sent to that owner with distance, displacement, velocity and lease checks.
State is scoped to the campaign, content IDs, connection generation and lease.
Snapshots stop predicting after a bounded interval rather than pinning an old
pose forever. A detached cell is not a deleted reference.

This is replication of supported physical body transforms and velocities. It
does not serialize the entire Havok solver, contacts, constraints or arbitrary
modded collision classes. A non-grab shove on the guest has no new verified
contact-origin hook; it is not independently promoted to authoritative input.

## Save barrier

1. Both original characters enter the native pause and quiesce gameplay.
2. Both incrementally capture movable placed references, managed drops and
   network-identified ragdolls in their attached cells. An unsupported physical
   graph or an unidentified temporary physical object rejects the checkpoint.
3. The server merges sorted logical keys and chooses each authoritative pose.
   Actor ownership and drop identity are checked against server state. The
   union retains a presence mask: a client does not force-load the other player's
   interiors, and must account for every physical object it actually captured.
4. The server streams the canonical frame in bounded chunks. Both clients apply
   it under the paused world's native nonblocking write-lock entry, then perform
   a second, independent native capture. Position, quaternion and linear/angular
   velocity floats must compare equal, with the same complete body graph.
   There is no network quantization or world-unit round trip in this path.
5. A worker hashes the canonical frame and writes a private `.tcphys` mapping
   next to that character's `.ess`. Only then does the client acknowledge the
   physical frame. The native save is requested only after **both** acknowledgments.
6. ESS, optional SKSE co-save and required TCPHYS are hashed twice for stability.
   The paired manifest binds both trios, the canonical frame digest and count,
   world, characters and calendar. Server physical records and the canonical
   frame are included in the immutable database checkpoint.

Native state remains paused between readback and ESS capture. `MarkChanged(4)`
is called outside the Havok lock, but it is not treated as proof that every
native save path preserves a ragdoll. The separately committed canonical frame
and load replay are required. This is not a byte-identical pair of ESS files.

## Load barrier

Before loading, each client verifies its complete native trio and its private
mapping's world/character/checkpoint/digest. After native load, the original
characters reconnect without releasing normal entity assignment or gameplay.
The server reads the canonical frame from the restored database timeline.
Each client resolves the native forms recorded in its own file, waits for the
correct loaded body graph, reapplies the canonical frame and independently
checks it again. Both matching digest acknowledgments are required to complete
the durable restore and resume gameplay.

Actor IDs inside a frame are checkpoint-local keys. Private native form IDs
are not network identities and are never sent as such. The private mapping
requires sorted unique physical keys and unique local forms. A missing form,
changed graph or failed readback cancels instead of inventing a replacement.
Failed post-load alignment leaves the durable restore pending and stops the
server; preserve the files and restart that matching server to retry the pair.

## Bounds and migration

- Protocol `0x5443000F`; manifest schema 2; SQLite schema 6. A nonempty database
  from older builds is refused. Use a separate new campaign database, retain the
  old database and complete save files with their matching build.
- 8,192 placed-reference records per campaign and at most 8,192 physical records
  per paired frame; 32 movable bodies per reference; 20 MiB canonical frame;
  121 attached cells reported per client. Capacity errors reject the operation,
  rather than silently marking an incomplete snapshot successful.
- Native application/capture is runner-only, shares the existing soft 500 us
  per-frame budget and uses bounded queues. Save scanning/application happens
  over multiple paused frames. A native call cannot be preempted midway.
- Exact readback is deliberately strict. A class whose native setter changes
  the requested floats will reject a checkpoint. Runtime acceptance, ragdoll
  initialization after native load, form-ID restoration and performance still
  require game testing; no tolerance has been widened to disguise a mismatch.
- Unloaded cells have no running local physics. Their last canonical placed
  records remain in the campaign; they are reseeded when observed. This is not
  a claim that all unloaded world cells are simulated or every mod is supported.

## Binary evidence

Only the 1.7.104 PE database was available through Ghidra MCP:
`SkyrimSE-1.7.104.exe`, x86 LE 64, SHA-256
`2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`.
Private evidence is retained outside the repository under
`D:/CodexTools/SkyrimCoop-RE/placed-checkpoint-20261119-mcp.json`.
No executable or decompiler dump is distributed.

Actually inspected through MCP in this change:

- Cell insert `1402D6F80` and remove `1402BFE20`, table erase `1402D5E60`:
  table at cell +88, 16-byte entries, occupied link at entry +8, retained ref +28.
- Cell lock/unlock IDs 19189/19190 (`1402CBED0`/`1402CBEF0`), underlying
  `1401B4D80`: recursive owner/count and single-CAS admission used by the scanner.
- Cell GetbhkWorld ID18995 (`1402BE9B0`): interior extra-world path and exterior
  world global. The returned wrapper matches the existing verified Havok view.
- TryLockForWrite ID68238 (`140CE34C0`): native boolean return, recursive writer
  and one CAS without a blocking retry. The blocking entry ID68234 is not used
  to acquire an unowned lock in the paused pump.
- World mark/unmark IDs77928/77929 (`14104FCE0`/`14104FD00`): native world-write
  wrappers at +C598. The paused pump pairs successful try-write with native unlock.
- Getter ID78079 (`1410588F0`) and pose setter ID78082 (`141058A40`) were reread:
  quaternion getter copies the native four floats; setter takes the normal world
  lock around the body dispatch. It can recursively enter the already-owned lock.
- TESObjectREFR constructor/vtable, native SaveGame `1402E0B10`, preparation
  `1402E0A40` and base save `1401E5700` were inspected. These do **not** establish
  complete native Havok persistence; hence the separate saved frame and replay.

Existing MCP evidence for body wrappers, graph traversal, transform/velocity
methods and scales is referenced in `PHYSICS_NPC_20261118.md` and
`DEEP_COOP_CHECKPOINT_20261117.md`. Those checks do not replace runtime validation.
