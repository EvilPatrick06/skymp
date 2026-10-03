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

Private merchant transfer control, trading panel UI and stock/payment work are
intentionally not part of this slice.