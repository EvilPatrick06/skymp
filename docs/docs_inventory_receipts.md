# Atomic inventory receipts and exact save acknowledgement

Merchant awards must not be uncertain after a crash. This slice adds a native
inventory compare-and-set that mutates inventory and a durable receipt together,
plus acknowledgement of the exact snapshot that finished writing.

`MpObjectReference::CompareAndSetInventory` requires a human profile. It refuses
when the live inventory or receipt dump does not match the caller's expected
values. Sequence numbers must advance within the safe JSON integer range.
Replacement inventories round-trip through JSON, stay within 128 KiB, keep item
metadata and refuse zero/invalid entries or count overflow. The receipt property
cannot be forged through ordinary property setters.

`WorldState::GetSavedInventoryReceipt` returns the receipt dump from the last
completed save for that actor and profile. Upsert completion updates the cached
acknowledgement from the submitted change-form snapshot, not from later live
mutations. Failed upserts leave acknowledgement unchanged and re-queue the form.
Destroying a form forgets its cached acknowledgement.

## Metadata-only compare-and-set

`compareAndSetActorProperty(formId, expectedProfileId, property)` commits a
registered hidden custom property without changing inventory, equipment or the
inventory receipt. Underscore-prefixed native properties and public properties
are refused. `property` requires `name`, `expected`, `replacement`,
`expectedLifeGeneration` and the canonical decimal `expectedRuntimeIdentity`.
Each JSON value is limited to 16 KiB. The actor's entry session is captured before
JavaScript getters or serialization, so callbacks cannot substitute a newer
session's token. Property comparison uses JSON semantics.

Success means the live property changed and a complete character snapshot was
queued. All snapshot copies and queue allocations occur before a nonthrowing live
commit. Suspended saves refuse the write. Success does not acknowledge a disk
write, and an unchanged saved inventory receipt cannot acknowledge later metadata.
The ordinary save queue serializes snapshots behind any in-flight batch and
preserves current inventory and receipts. Goods and rewards must continue using
the atomic inventory/property transaction and its exact save acknowledgement.

Private merchant transfer control, trading panel UI and stock/payment work are
intentionally not part of this slice.
