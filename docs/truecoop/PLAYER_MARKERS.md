# Native party-player markers

The client now binds the ordinary Skyrim quest-target system to party members. It supplies compass, world-map and floating world pointers through the game's existing HUD. These follow the user's normal quest-marker display settings. No replacement compass/map SWF or custom render-thread engine access is introduced.

This is implemented for the MCP-verified 1.7.104 runtime. It still needs verification in two real games: first/third person, interiors, world/local maps, door routing, distant players, reconnect, death and save/load. No native UI/frame-time result is claimed from the headless probe.

## Data path and lifetime

- The server sends one bounded full party-location snapshot every 500 ms, even across cells/worldspaces. It contains at most eight ready assigned players. Only party members receive it.
- Each entry is taken from the server's accepted movement state and current ownership epoch. It includes the server receipt time and the cell/world in which that position was observed. A cell change cannot reinterpret the previous cell's coordinates as a position in the new cell. Wrong owner/epoch, replayed movement and old observations cannot refresh a marker.
- The client rejects a stale world envelope, old snapshot sequence, another party leader, non-member entries, invalid coordinates and samples older than five seconds. Local expiry uses GetTickCount64 independently of a rebased networking clock. Party leave/disconnect cleans the targets.
- Nearby markers bind directly to the remote player actor for normal native movement. A distant player binds to an owned temporary reference of a new model-free STAT base. It has no model, physics, inventory, script or AI; the reference is not a substitute actor. At most one anchor is created per 250 ms service tick. Position samples do not spawn/recreate the reference; changing its cell/world does.
- Every handle is resolved for the operation with ScopedReference. No engine pointer survives a tick. Only our own anchor base may be deleted. Aliases clear before anchors are deleted. On initial cleanup, old anchors restored in quest aliases are also removed, even though the new session's slots do not know their handles yet.
- All engine calls occur on the existing world runner/update thread. The native renderer draws the stock marker. Native engine functions contain their own locks; no zero-stutter guarantee or frame-time bound is inferred from static analysis.

## Bundled data

`Tools/TrueCoop/build_player_markers.py` adds QUST `TrueCoopPlayerMarkers` (local ID 0x4DF4) and STAT `TrueCoopPlayerMarkerAnchor` (0x4DF5) to the bundled GPL SkyrimTogether.esp. The quest has eight optional reference aliases/objectives, no stages/fragments/scripts and no inventory/faction/AI changes. Optional aliases never make players essential, protected, reserved or quest objects. The model-free anchor has only EDID and zero OBND.

All original records remain byte-identical; only TES4's record count/next ID and the QUST group size change. The generator verifies preservation and idempotence and refuses a conflicting reserved ID. Tests pin the original 6420-byte ESP and generated 7530-byte ESP hashes.

The known original SHA256 `3196266aa29855574bda48335da9ab2f18ad33a80eee116400da7c5c81c93d7e` upgrades to `9ef8a8f8bb08dca12f43355e073adf0a3bd69cb4f947964fec10a278a1f7694b` in a restored 1.7.104 campaign contract. Only that exact, non-ESL SkyrimTogether.esp with its original two masters is upgraded. The contract commit is atomic and idempotent; original object inventory/locks and world ID survive reopening. Unknown edits, other plugins/resources, downgrades and pinned quest adapters incompatible with the new digest remain rejected. This does not relax the requirement for matching active content on both players.

The quest's empty stage list excludes it from the shared campaign journal using the existing QuestService rule. The generated ESP must be deployed alongside this client; an old ESP is not sufficient. No new plugin needs enabling.

## Reverse-engineering evidence

Ghidra MCP is reached through the read-only stdio bridge after opening the existing CodeBrowser. Only 1.7.104 is available; every call verifies the PE image base and executable hash before/after. Local evidence is in `D:\CodexTools\SkyrimCoop-RE\marker-*-results.json`.

- SetObjectiveDisplayed registration at 140A60D30 identifies callback 140A5F8F0: `(VM*, stack, quest*, uint32 objective, bool displayed, bool force)`. It finds the objective and calls its native state transition, 14039B500. State 1 drives the ordinary player objective list/HUD. Normal engine behavior may display objective notifications.
- ReferenceAlias ForceRefTo callback 140A1B6A0 reads owning quest +0x10 and alias ID +0x18, then calls ID25052 / 1403D4FA0 `(quest, uint32 alias, reference)`. Its body and the alias-map update were examined.
- ReferenceAlias Clear callback 140A1B5D0 requires optional flag bit 1 at alias+0x1C, then calls 1403D4BC0 to release the alias and promoted reference. It does not accept None through ForceRefTo.
- Existing GetAliasedRef ID25066 and EnsureQuestStarted ID25003 are reused; their earlier MCP evidence/layout is retained.
- Factory ID13723 / 1401BC1C0 was rechecked. Its exterior lookup 1401B4B50 is a locked cache lookup, with no LoadCell path. Its fallback 1403106F0 constructs/registers the world persistent cell if needed, without terrain/3D cell streaming. The anchor has no mesh/body to load.
- SetPosition ID19790 / 1402F0EF0 updates a TESObjectREFR position and change state. The existing hook previously accessed ActorExtension before checking the form type; it now returns to the native path first for non-actors, including anchors and dropped objects.

## Validation at this stage

- Native client/launcher, server/DLL/runner and protocol probe built.
- 91 Catch2 cases / 2595 assertions passed (new checked location codec and atomic core-contract upgrade checks included).
- Five asset tests pass: pinned hashes, deterministic/idempotent generation, byte preservation of existing records, no stages/scripts/AI flags/model, conflicting IDs and truncation rejection.
- `TrueCoopProtocolProbe --verify-locations`: eleven scenarios pass, including cross-interior delivery, no relabelled old-cell coordinates, wrong sender/epoch/replay rejection, party privacy, expiry, recovery and party removal.

Full item physics, ragdoll/furniture/navmesh, paired native save rollback and quest item/reward custody are separate remaining modules. Alternate Start is excluded from the latest requested scope and was not modified.
