# Private merchant transfer prototype

This native controller is experimental and is not enabled on Dev. It does not
open a trading panel, authenticate an offer, select prices, or pay quests.
Thornswood's private merchant ledger remains authoritative for the 249 NPC
purses and purchased goods. Native NPC equipment and loot are not that ledger.
The controller must not be connected to trading without that integration.

The type is header-only at
`skymp5-server/cpp/server_guest_lib/MerchantTransferController.h`.
`WorldState::GetMerchantTransfers` binds its receipt and identity lookups.
Clearing or destroying the world revokes outstanding controller callbacks.

## Preparation and debit

`Begin` requires distinct live forms, a positive human profile, and a
server-controlled NPC merchant. It binds the actual form instances and profile,
validates both complete inventories and deltas, and prepares every resulting
snapshot before a debit. Metadata is preserved. Zero base ids, empty entries,
count overflow, snapshots over 128 KiB, and unavailable receipt sequences are
refused. At least one delta must be nonempty; a free offer can have no debit.

Both form ids stay locked. Pending, quarantined, awaiting-save, refused and
offline records count toward the limit of 25. Durable records do not count.

`CommitHumanDebit` rechecks both identities and the prepared merchant snapshot.
It applies the human debit through `CompareAndSetInventory`. Its receipt is
prepared before mutation. An exception after an inventory update retains the
attempt and locks for reconciliation. A refused compare-and-set leaves the
pending offer unchanged.

`IsHumanDebitDurable` requires the exact saved debit receipt. A live receipt
alone never acknowledges persistence.

## Settlement and acknowledgement

`ReleaseQuarantine` requires the saved debit receipt, both original identities,
and the prepared merchant inventory. It compare-and-sets the final human
snapshot at the next sequence, including when the credit is empty. It updates
the merchant with `SetInventory`, without writing a human receipt on the NPC.

The offer then awaits saving with both actors locked. WorldState captures an
acknowledgement only when the submitted batch contains both exact final
snapshots, their identities and the final human receipt. Successful completion
of that batch marks the offer durable and unlocks it. Failure retains the
locks. A synchronous submission exception requeues consumed live changes.
Stale or duplicate callbacks cannot unlock a newer offer.

`Refuse` and `MarkOffline` retain the record and locks. `Forget` cannot erase an
attempted debit before durability. Pruning a completed record cannot remove
locks belonging to a newer transfer.

## Remaining release blockers

Offers and locks are process-local. There is no durable transaction intent,
restart reconstruction, or resume operation for held offers. A merchant
publication exception after human credit requires reconciliation and remains
locked. These limitations prevent enabling this prototype for buying, selling,
or quest payments. Passing native regressions does not complete Thornswood
issues 1375 or 505.

The next integration must persist intent in the canonical private ledger,
coordinate a single full human inventory compare-and-set with exact save
acknowledgement, and recover after restart without repeating payment. It must
also bind the authenticated trading panel to the submitted hold price books.
