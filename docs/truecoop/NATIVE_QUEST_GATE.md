# Native quest item gate — implementation, not gameplay validation

The exact `SkyrimSE-1.7.104.exe` PE is open in the existing Ghidra project.
MCP metadata reports 176587 functions and SHA-256
`2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`.
There is no 1.6.1170 database available. No EXE or decompiler dump is shipped.

## Verified through actual Ghidra MCP

| Address / Address Library ID | Evidence used by the implementation |
|---|---|
| `1419A7F40` / vtable `218372` | Unique full-byte match for the COL pointer and all seven AddItemFunctor methods. Preserve COL at vtable[-1], not just the callable slots. |
| `140A415C0` / `56095` | Run initializes a fresh Variable output; target +10, item +14, count +18, silent +1C, retained VM +20. The existing trampoline executes the original. |
| `140A4FA30` / `56348` | Destructor releases the VM and base, frees 28 bytes only with flags&1, returns this. Native destruction is forwarded; no guessed C++ delete or extra VM release. |
| `140A4FA10`, `1409B2670` | Original IsLatent=true, WantsRequeue=false. |
| `140A41720` / `56096`, `140A417F0` / `56097`, `140A4FA20` / `56347` | Preserve native Save, Load and type 14 methods. |
| `1409D8520`, `1409E7DF0` | Native queue swap/drain releases queue lock before Run. WantsRequeue transfers to the next queue without returning to the VM. Assembly explicitly loads this in RCX for requeue/latent/destructor calls. |
| `14061DDA0`, `14061DE80` | Skyrim writes all nonconstant globals by form and restores their float values, independently of per-form change flags. Rechecked this continuation for saved receipts. |

Evidence files stay outside the repository in `D:/CodexTools/SkyrimCoop-RE/`:
`quest-native-gate-20261118-mcp.json`,
`quest-native-gate-scheduler-20261118-mcp.json`,
`quest-grant-saved-globals-20261118-mcp.json`.

## Implemented behavior

`NativeQuestGate` clones the complete per-instance table, changing only Run,
WantsRequeue and the destructor. It bounds native-held operations to 128 and
disposal notices to 256. A policy sees copied provenance and an ephemeral cookie,
never a native pointer. Waiting ticks poll the cookie; they do not repeatedly
read the VM stack or copy script names. Callbacks execute outside the gate lock.
No blocking network wait or sleep is introduced.

An admitted runner can execute the original effect once, continue requeueing
until receipt resolution, or suppress it and finish. Only Run changes the Done
bit; a response arriving between Run and WantsRequeue cannot wake the VM early.
The table is restored before forwarding the real destructor. All live cloned
functors, including the interval before native resume/destruction, block saves.
Cancellation, overflow or partial failure blocks saves until an actual native
load, not merely a load request. Load generations prevent stale delivery cleanup
from marking the new world dirty. A recovery load is allowed when only the
sticky failure remains, with no live operation.

## Boundaries

**Historical classified-policy status:** no Key/Reward semantic policy was
configured at this checkpoint. The later accelerated Shared policy now handles
standard base-form AddItem/RemoveItem from shared quests; see
`ACCELERATED_QUESTS_RU.md`. It preserves a separate complete native Remove table
and destructor. Unknown/unsupported source paths keep their original behavior.
The older description below applies to the classified producer. Unknown/unclassified AddItem calls keep their
original behavior. The gate is not evidence that vanilla rewards are already
intercepted, classified or distributed. The gate and receipt service still need
the admitted broker, verified adapter call sites, actual-effect collection,
server permission/commit and both-recipient completion acknowledgement.

Runtime thread affinity, native teardown after load, competing SKSE detours,
quest fragments and frame costs still require gameplay checks. Native locks,
inventory traversal, native saves and the engine's own scheduler still take
time; this work makes no zero-stutter guarantee. All tests remain deferred at
the user's request; production compilation is recorded separately.
