# Private merchant transfer

This is the native controller for one human and one merchant NPC. It does not
open a trading panel, price books, or quest payments. Those stay on
Thornswood #1375. The type is header-only:
`skymp5-server/cpp/server_guest_lib/MerchantTransferController.h`.

`WorldState::GetMerchantTransfers` constructs the controller and binds
`UseParentSavedReceipt` to `GetSavedInventoryReceipt`. Durability checks do
nothing until that hook is bound.

The human side uses `CompareAndSetInventory` and the inventory receipt.
The merchant side uses `SetInventory` only. That edit already queues the
existing save batch. The controller never calls `CompareAndSetInventory` on
the merchant and never writes `_inventoryReceipt` there.

`humanDebit` is the stack the human gives up. `humanCredit` is the stack the
human receives. Removal is applied before addition, so a spent stack cannot
fund itself. Metadata stays on the matched entry. Empty entries, a zero
base id, a count that overflows, and a snapshot over 128 KiB are refused.

## Calls

`Begin` requires two different forms and a human profile. The debit must be
non-empty. It locks both form ids. A second transfer on either actor fails
while the lock is held. Pending, quarantined, refused, and offline records
count toward the cap of 25. A full cap does not lock and does not store the
offer. Durable records do not count.

`CommitHumanDebit` runs only for a pending offer for that human. It removes
the debit from the expected inventory and compare-and-sets that result with
the expected receipt and sequence. The credit is not applied. On success the
offer stores the post-debit inventory and the live receipt dump and becomes
quarantined. A mismatch or a thrown compare-and-set leaves the offer pending
and keeps the lock.

`IsHumanDebitDurable` is true only for a quarantined or durable offer when
`GetSavedInventoryReceipt` equals the receipt dump stored at debit time.

`ReleaseQuarantine` requires that saved receipt. It compare-and-sets the
human credit at the next sequence, using the post-debit inventory and the
stored receipt. An empty credit skips that compare-and-set. It then
`SetInventory`s the merchant: the merchant gains the human debit and loses
the human credit. The offer becomes durable and the pair is unlocked. If the
merchant edit throws after the human credit succeeded, the offer stays
quarantined.

`Refuse` and `MarkOffline` apply only to a pending or quarantined offer.
They keep the lock, so the record still fills the cap until `Forget`.
`Forget` unlocks the pair and drops the record.