# Atomic world-item inventory effects — 18 November 2026

Update 19 November: the bounded ordinary-item runtime service is now connected.
See `ACCELERATED_BUILD_20261119_RU.md` for its active scope and untested limits.
The implementation notes below describe the preceding persistence-only stage.
The user deferred all tests; only source inspection and production compilation
were performed. No installed game, profile, save or world database was changed.

`Campaign::State::TransferWorldItem` commits four records together: the object's
lifecycle, one immutable inventory receipt, its recipient inbox and its operation
index. A negative delta creates a drop; a positive delta conditionally claims an
existing stack at its last committed lifecycle revision. Both results retain the
same logical item ID. Full pickup makes a tombstone; ordinary motion writes cannot
resurrect it or change an item's inventory quantity/variant.

A transfer contains a saved character recipient, a separate random session/load
identity and a nonzero operation nonce. A repeated matching operation returns its
original receipt; changed recipient/session, ID, delta, expected revision or drop
payload conflicts. Session identity prevents an operation counter restarted at one
from matching a previous process's receipt. This is correlation, not authentication:
the future network service must supply only the authenticated saved recipient and
check native ownership, range, capacity and party policy before calling the store.

Stack counts are limited to 32767 for the native signed ExtraCount representation.
Quest objects and worn payloads are refused. Item/enchantment/poison/effect forms are
checked against the pinned content contract. The receipt embeds the committed
lifecycle snapshot; its trusted revision comes from the database row. Snapshot
IDs, recipient registration, current lifecycle and both index directions are
validated on open and after paired restore. A read returns at most 64 receipts in
revision order and never splits one recipient's transaction.

SQLite schema **4** fences older schema-3 servers. The layout of record tables is
unchanged; their new durable semantics must not be silently ignored by an old
server. Existing supported schemas advance on open, using the existing WAL/FULL
transaction/checksum machinery. Item lifecycle/receipts/indexes roll back together
with the campaign checkpoint; revision allocation and participant registrations
remain outside the rolled-back timeline. An exact additive hash migration also
recognizes our thirteen-global predecessor of TrueCoopWorldState.esp. Unknown
plugin edits stay incompatible.

The reserved native item cursor is separate from the quest-grant cursor. An empty
quest inbox must never acknowledge an unprocessed item receipt. `WorldItemRegistry`
provides 64 native-save aliases and 64-bit logical IDs as four exact uint16 globals
per slot; its calls are admitted only on the runner and partial writes block saves
until actual load. It does not patch private alias handles or use a temporary
runtime FormID as a network identity.

## Required before enabling gameplay

- Connect authenticated lifecycle/claim/inbox/receipt-ACK RPCs and a bounded native
  item service. The persistence API currently has no gameplay callers.
- Defer the original player's drop until a durable decision, or retain a fully
  fenced original-execution proof; do not remove inventory twice on a live receipt.
  Native drop ID40454 can return no world reference after moving an item into a
  special container, so a zero result alone does not prove no inventory effect.
- Apply/read back one native inventory effect and advance its saved cursor under
  a native-save barrier. A socket ACK can update transient server actor inventory
  once; it must never delete the immutable receipt. Wait for native actor baseline
  assignment before replay so ACK effects cannot also be included in that baseline.
- Restore alias mappings incrementally, handle partial/full claims, spawn/reference
  metadata, disconnects, native loads and custom-enchantment/display-name limits.
  The native factory's ninth argument is ExtraPrimitive, not ExtraDataList.
- Wire owner-authorized single-body motion and guest interaction; add exact common
  checkpoint coordination. Rig physics alone does not complete loose-item physics.
- Perform the deferred storage, two-client gameplay and frame-time checks.

MCP inspected the exact 1.7.104 native drop/pickup, factory, count setter,
ForceAlias, native extra charge/health/enchantment/poison/soul setters. Charge's
assembly confirms XMM1 despite an untyped decompiler argument. These checks do not
prove native gameplay application, arbitrary item-script behavior or crash recovery.
Evidence is outside the repository under D:/CodexTools/SkyrimCoop-RE; no EXE or
decompiler dump is distributed.
