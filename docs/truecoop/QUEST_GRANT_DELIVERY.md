# Quest grant inbox and saved cursor

Implemented source path: immutable server ledger → bounded read-only inbox →
recipient validation → admitted runner inventory application/readback → native
saved revision cursor. This is **not yet automatic vanilla quest reward sync**:
the semantic adapter/broker that produces real approved plans is not connected.

## Protocol and storage

Protocol is `0x5443000B`. `RequestQuestGrants` contains only a nonce, saved
revision and cursor capability. It cannot specify a recipient or submit item
roles. The recipient is the authenticated persistent character. The server
reads the validated durable inbox and sends one receipt (at most 64 KiB), or
an empty response with the current world head. Queries are rate-limited per
connection. Both the request envelope's world epoch and reply nonce fence old
responses. A retry reads the same immutable row. No socket ACK deletes a grant.

`TrueCoopData` separates bounded codecs from the SQLite service, so client
decoding does not link/open the server's database. Existing stored receipt
encoding remains unchanged; the trusted row revision travels in the reply.
World rollback preserves original row revisions.

## Cursor plugin

`TrueCoopWorldState.esp` is our own standalone plugin, master Skyrim.esm,
13 quest-receipt globals, zero overrides. The physics continuation also stages
64 optional object aliases, 256 object-ID globals and five separate item-receipt
globals in this same plugin; their dropped-object service is not connected yet.
`build_grant_cursor.py` produces it
from `QuestGrantCursorIds.h` and refuses to overwrite unexpected bytes. The
globals store a 32-digit world UUID as eight 16-bit words, a revision as four
16-bit words and a version marker. Every 16-bit value is exactly representable
as a float. Native save/load stores them with inventory, not in a sidecar file.

Binding accepts an all-zero cursor or the same world. It never silently resets
a foreign world. Reads validate plugin, form type, editor names, finite integral
words, version and signed SQLite revision range. Writes use the existing
MCP-verified GlobalVariable.SetValue callback and read back the complete value.

Both clients need this same plugin and content contract. Adding it changes the
active content fingerprint: use a new isolated campaign/profile, or an explicit
future migration. Do not silently reuse or delete an old campaign database.
A legacy profile without the plugin can play an empty inbox; it is refused if
there is a saved grant to deliver.

## Native application boundary

The client preflights the entire projected receipt: recipient, quest, mapped
forms, native item/enchantment/poison/effect types and count ranges. It applies
at most one item per runner tick, compares item-variant counts before/after,
then advances the saved cursor. Existing owned-player inventory hooks relay
the delta normally; remote actor replay does not create another grant.

The native save hook and common save preparation block while application is
incomplete. A disconnect or failed readback after mutation leaves a sticky
save barrier until an actual ESS load. Before any mutation, a bad receipt
fails without changing inventory. Interrupted applications never silently retry
against the same partially modified runtime. Advancing an empty inbox to the
observed head is safe because a later committed grant gets a later revision.

A bounded in-memory original-execution marker avoids running that original
effect again in the same runtime. It survives a socket reconnect and is cleared
by native load. It is not a saved receipt or a replacement for the future
broker's effect validation. There is no caller recording these markers yet.

Quest keys needing native aliases/references are refused on replay unless
their original native execution is available. Dynamic inline enchantments are
also refused at present. No base-form copy pretends to restore an alias. The
current Inventory DTO does not contain all arbitrary mod metadata/display names.
XP, spells, direct alias-fill rewards, policy matching, repeatable occurrences,
gate completion and crash recovery admission still need integration. These
limits prevent claiming the full quest-items/rewards requirement is done.

No gameplay/protocol/unit tests have been run for this continuation, as requested.
Compilation is not proof of correct native serialization, inventory behavior,
two-player reward delivery or frame-time cost.
