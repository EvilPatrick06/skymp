#include "MerchantTransferController.h"

#include "MpActor.h"
#include "MpObjectReference.h"
#include "WorldState.h"

#include <algorithm>
#include <stdexcept>

namespace {
Inventory ApplyTransfer(const Inventory& base, const Inventory& remove,
                        const Inventory& add)
{
  Inventory next = base;
  next.RemoveItems(remove.entries);
  next.AddItems(add.entries);
  return next;
}
}

MerchantTransferController::MerchantTransferController(WorldState& worldState_)
  : worldState(worldState_)
{
}

bool MerchantTransferController::CountsTowardCap(State state) const
{
  return state == State::Pending || state == State::Quarantined ||
         state == State::Refused || state == State::Offline;
}

uint32_t MerchantTransferController::PendingCount() const
{
  uint32_t n = 0;
  for (const auto& offer : offers) {
    if (CountsTowardCap(offer.state)) {
      ++n;
    }
  }
  return n;
}

bool MerchantTransferController::IsLocked(uint32_t formId) const
{
  return std::find(locks.begin(), locks.end(), formId) != locks.end();
}

bool MerchantTransferController::TryLockPair(uint32_t humanFormId,
                                             uint32_t merchantFormId,
                                             std::string& reason)
{
  if (IsLocked(humanFormId) || IsLocked(merchantFormId)) {
    reason = "Actor already locked for a merchant transfer";
    return false;
  }
  locks.push_back(humanFormId);
  locks.push_back(merchantFormId);
  return true;
}

void MerchantTransferController::UnlockPair(uint32_t humanFormId,
                                            uint32_t merchantFormId)
{
  locks.erase(std::remove(locks.begin(), locks.end(), humanFormId),
              locks.end());
  locks.erase(std::remove(locks.begin(), locks.end(), merchantFormId),
              locks.end());
}

std::optional<MerchantTransferController::Offer> MerchantTransferController::Find(
  uint64_t offerId) const
{
  for (const auto& offer : offers) {
    if (offer.id == offerId) {
      return offer;
    }
  }
  return std::nullopt;
}

MerchantTransferController::Result MerchantTransferController::Begin(
  MpActor& human, MpObjectReference& merchant, const Inventory& humanDebit,
  const Inventory& humanCredit, const Inventory& expectedHumanInventory,
  const std::string& expectedHumanReceipt, uint64_t expectedHumanSequence)
{
  Result result;
  if (PendingCount() >= kMaxPendingRecords) {
    result.reason = "Pending merchant transfer cap reached";
    return result;
  }

  auto* merchantActor = merchant.AsActor();
  if (!merchantActor || !merchantActor->IsServerControlled() ||
      merchantActor->IsCreatedAsPlayer() || human.GetProfileId() <= 0) {
    result.reason = "Transfer requires a human profile and merchant NPC";
    return result;
  }

  std::string reason;
  if (!TryLockPair(human.GetFormId(), merchant.GetFormId(), reason)) {
    result.reason = reason;
    return result;
  }

  Offer offer;
  offer.id = nextOfferId++;
  offer.humanFormId = human.GetFormId();
  offer.merchantFormId = merchant.GetFormId();
  offer.humanDebit = humanDebit;
  offer.humanCredit = humanCredit;
  offer.expectedHumanInventory = expectedHumanInventory;
  offer.expectedHumanReceipt = expectedHumanReceipt;
  offer.expectedHumanSequence = expectedHumanSequence;
  offer.state = State::Pending;
  offers.push_back(offer);

  result.ok = true;
  result.offerId = offer.id;
  return result;
}

MerchantTransferController::Result
MerchantTransferController::CommitHumanDebit(uint64_t offerId)
{
  Result result;
  result.offerId = offerId;
  for (auto& offer : offers) {
    if (offer.id != offerId) {
      continue;
    }
    if (offer.state != State::Pending) {
      result.reason = "Offer is not pending";
      return result;
    }

    try {
      auto& human = worldState.GetFormAt<MpActor>(offer.humanFormId);
      Inventory next = ApplyTransfer(offer.expectedHumanInventory,
                                     offer.humanDebit, offer.humanCredit);
      const bool swapped = human.CompareAndSetInventory(
        offer.expectedHumanInventory, offer.expectedHumanReceipt, next,
        offer.expectedHumanSequence);
      if (!swapped) {
        UnlockPair(offer.humanFormId, offer.merchantFormId);
        offer.state = State::Refused;
        result.reason = "Human inventory compare-and-set refused";
        return result;
      }
      offer.state = State::Quarantined;
      result.ok = true;
      return result;
    } catch (const std::exception& e) {
      UnlockPair(offer.humanFormId, offer.merchantFormId);
      offer.state = State::Refused;
      result.reason = e.what();
      return result;
    }
  }
  result.reason = "Unknown offer";
  return result;
}

bool MerchantTransferController::IsHumanDebitDurable(uint64_t offerId) const
{
  for (const auto& offer : offers) {
    if (offer.id != offerId) {
      continue;
    }
    if (offer.state != State::Quarantined && offer.state != State::Durable) {
      return false;
    }
    try {
      auto& human = worldState.GetFormAt<MpActor>(offer.humanFormId);
      Inventory next = ApplyTransfer(offer.expectedHumanInventory,
                                     offer.humanDebit, offer.humanCredit);
      // Rebuild the receipt dump the same way CompareAndSetInventory writes it.
      // Durability is acknowledged only by GetSavedInventoryReceipt matching
      // the live receipt after a successful human CAS.
      const auto saved = worldState.GetSavedInventoryReceipt(human);
      return saved == human.GetInventoryReceiptDump() &&
             human.GetInventory().ToJson() == next.ToJson();
    } catch (...) {
      return false;
    }
  }
  return false;
}

MerchantTransferController::Result
MerchantTransferController::ReleaseQuarantine(uint64_t offerId)
{
  Result result;
  result.offerId = offerId;
  for (auto& offer : offers) {
    if (offer.id != offerId) {
      continue;
    }
    if (offer.state != State::Quarantined) {
      result.reason = "Offer is not quarantined";
      return result;
    }
    if (!IsHumanDebitDurable(offerId)) {
      result.reason = "Human debit is not durable yet";
      return result;
    }

    try {
      auto& merchant =
        worldState.GetFormAt<MpObjectReference>(offer.merchantFormId);
      // Merchant gains what the human lost and loses what the human gained.
      // Never CompareAndSetInventory and never write _inventoryReceipt.
      Inventory next = ApplyTransfer(merchant.GetInventory(), offer.humanCredit,
                                     offer.humanDebit);
      merchant.SetInventory(next);
      offer.state = State::Durable;
      UnlockPair(offer.humanFormId, offer.merchantFormId);
      result.ok = true;
      return result;
    } catch (const std::exception& e) {
      UnlockPair(offer.humanFormId, offer.merchantFormId);
      offer.state = State::Refused;
      result.reason = e.what();
      return result;
    }
  }
  result.reason = "Unknown offer";
  return result;
}

MerchantTransferController::Result MerchantTransferController::Refuse(
  uint64_t offerId, const std::string& reason)
{
  Result result;
  result.offerId = offerId;
  for (auto& offer : offers) {
    if (offer.id != offerId) {
      continue;
    }
    UnlockPair(offer.humanFormId, offer.merchantFormId);
    offer.state = State::Refused;
    result.ok = true;
    result.reason = reason;
    return result;
  }
  result.reason = "Unknown offer";
  return result;
}

MerchantTransferController::Result MerchantTransferController::MarkOffline(
  uint64_t offerId)
{
  Result result;
  result.offerId = offerId;
  for (auto& offer : offers) {
    if (offer.id != offerId) {
      continue;
    }
    UnlockPair(offer.humanFormId, offer.merchantFormId);
    offer.state = State::Offline;
    result.ok = true;
    return result;
  }
  result.reason = "Unknown offer";
  return result;
}

MerchantTransferController::Result MerchantTransferController::Forget(
  uint64_t offerId)
{
  Result result;
  result.offerId = offerId;
  for (auto it = offers.begin(); it != offers.end(); ++it) {
    if (it->id != offerId) {
      continue;
    }
    UnlockPair(it->humanFormId, it->merchantFormId);
    offers.erase(it);
    result.ok = true;
    return result;
  }
  result.reason = "Unknown offer";
  return result;
}
