# Saved character identity

This is a prerequisite for player checkpoints, item custody and reward receipts. It does **not** implement paired native saves, inventory ownership transactions or exactly-once quest rewards.

The 1.7.104 client stores a random 128-bit ID in eight own nonconstant GLOB records (`TrueCoopCharacterPart0..7`, local IDs 0x4DF6..0x4DFD). Every word is an exactly representable integer float in [0,65535]. A ninth global, `TrueCoopCharacterVersion` (0x4DFE), is the schema/initialization marker. Names, types, flags, finite/integer ranges, version and nonzero ID are checked. Partial or unknown layouts fail closed. No quest script, VM variable layout, new SKSE plugin, private co-save hook or file writer is added.

On connection the runner reads copied words, the existing manifest worker obtains system randomness if the loaded character is unassigned, and the runner invokes the registered GlobalVariable.SetValue callback before sending authentication. Engine forms/pointers never cross to that worker. There is no disk IO, lock acquisition or wait in the native commit. A newly initialized ID becomes durable when the player saves normally. Saves predating initialization remain unassigned and can create another ID; they are not automatically migrated on disk. Copying an initialized save copies its logical character identity.

Authentication carries the fixed 128-bit value after the protocol prefix. Protocol is 0x54430008. Truncated/zero IDs are refused. The server rejects two simultaneous sessions of one saved character, persists enrollment under `$participant/<32 hex digits>` and retains that identity after reconnect/restart. Enrollment is idempotent and does not consume another DB revision on reconnect. The ID is not an account credential or anti-cheat claim.

The existing native TESLoadGameEvent now immediately invalidates queued gameplay generations and pending manifest/admission work through atomics. Its runner consumes a completion flag before polling more network packets and closes the old session. The player reconnects after a local load. This is a post-load guard, **not** a verified pre-load hook or coordinated two-client rollback; native loading already in progress is not stopped. No saved positions or inventory are restored by this feature.

## Ghidra MCP evidence

Only the available SkyrimSE-1.7.104.exe database was used: PE base 0x140000000, SHA256 `2c9cb9d61f3721ffb43a234764fe112864a89f4d63579c31fd028a3720047679`. The bridge checked the executable before and after every batch.

- GlobalVariable registration 0x140A39F10 installs `void(VM*, uint32 stack, TESGlobal*, float)` callback 0x140A39EC0. Ghidra marks the short callback as LAB rather than a standalone function; the complete local 80-byte region was independently matched at that exact address with MCP search_bytes. It checks the constant bit at +0x10, writes the float at +0x34 or reports a VM error. It does not MarkChanged. Existing Papyrus registration capture supplies the callback; no raw address enters the client.
- TESGlobal loader 0x1403409A0 checks native form type 9, reads FNAM at +0x30 and FLTV at +0x34. The vtable (Address Library 191675 => 0x14183A0C8) was also matched through MCP. The existing TESGlobal layout's offsets are asserted.
- Native global saver 0x14061DDA0 iterates the data handler's global array, excludes only constant globals, writes each global's form ID and exact four-byte value, then writes the count. It does not require a per-form dirty flag.
- Native global loader 0x14061DE80 resolves the saved form ID, checks TESGlobal and the constant flag, then restores four bytes to +0x34. Ordinary per-form TESForm Save/Load does not serialize the global value and must not be substituted for this global-data path.
- Local evidence: `identity-register-results.json`, `identity-setvalue-byte-results.json`, `identity-vtable-results.json`, `identity-global-serialize-results.json` under the user's RE directory. No game binary, dump or project database is committed.

Assumptions still needing gameplay proof: actual save/load sequencing with other native/SKSE mods, concurrent saving during the one-time initialization, menu notification behavior, and restoration of identity across a real .ess save/load. New identity initialization uses the same native setter as Papyrus; no unverified custom save lock is acquired. Native engine calls may contain their own work; static analysis is not an FPS measurement.

## Additive data and verification

The deterministic builder preserves every prior record byte. The exact 6420-byte original core and 7530-byte marker-only core can upgrade to the 8186-byte core SHA256 `ff52f44a06d57bded74a2a306966844845ec254d8b9ae015b30ec493ff224502`. No other plugin/resource digest is changed or accepted. Current/unknown hashes never downgrade. Existing persistent object state/world ID remain intact.

Critical tests cover float boundaries/corruption, every truncated identity prefix, zero IDs, wire equality, enrollment across DB restart, idempotent reconnect, and both exact additive migrations. Six asset checks pin generated hashes, all preserved records, nine nonconstant script-free globals, conflicts and idempotence. Real Skyrim gameplay is not executed by these tests.

Alternate Start is excluded by the user and unchanged.
