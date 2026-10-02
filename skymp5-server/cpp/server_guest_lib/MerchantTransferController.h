#pragma once
#include "Inventory.h"
#include "MpActor.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

class WorldState;

// Private merchant transfer between one human and one merchant NPC.
//
// Human debit and later human credit go through CompareAndSetInventory.
// Durability is GetSavedInventoryReceipt, bound with UseParentSavedReceipt
// once WorldState is complete (GetMerchantTransfers is the place).
// Merchant inventory is replaced with SetInventory, which queues the
// existing save batch. Never CompareAndSetInventory on the merchant,
// and never write _inventoryReceipt there.
//
// The human credit stays off the human until that debit receipt is the
// saved one. Pending, offline and refused records count toward the cap.
class MerchantTransferController
{
public:
  static constexpr uint32_t kMaxPendingRecords = 25;
  static constexpr uint64_t kMaxSequence = 9007199254740991ull;

  enum class State
  {
    Pending,
    Quarantined,
    Durable,
    Refused,
    Offline
  };

  struct Offer
  {
    uint64_t id = 0;
    uint32_t humanFormId = 0;
    uint32_t merchantFormId = 0;
    Inventory humanDebit;
    Inventory humanCredit;
    Inventory expectedHumanInventory;
    Inventory humanAfterDebit;
    std::string expectedHumanReceipt;
    std::string submittedReceipt;
    uint64_t expectedHumanSequence = 0;
    State state = State::Pending;
    std::string reason;
  };

  struct Result
  {
    bool ok = false;
    uint64_t offerId = 0;
    std::string reason;
  };

  explicit MerchantTransferController(WorldState& worldState_)
    : worldState(worldState_)
  {
  }

  // Call from GetMerchantTransfers. Binds acknowledgement to
  // WorldState::GetSavedInventoryReceipt without this header naming
  // that method while WorldState.h is still opening.
  template <class World>
  void UseParentSavedReceipt(World& world)
  {
    savedReceipt = [&world](MpObjectReference& ref) {
      return world.GetSavedInventoryReceipt(ref);
    };
  }

  Result Begin(MpActor& human, MpObjectReference& merchant,
               const Inventory& humanDebit, const Inventory& humanCredit,
               const Inventory& expectedHumanInventory,
               const std::string& expectedHumanReceipt,
               uint64_t expectedHumanSequence)
  {
    Result result;
    if (human.GetFormId() == 0 || merchant.GetFormId() == 0 ||
        human.GetFormId() == merchant.GetFormId()) {
      result.reason = "human and merchant must be two different forms";
      return result;
    }
    if (human.GetProfileId() <= 0) {
      result.reason = "inventory transactions require a human profile";
      return result;
    }
    if (humanDebit.IsEmpty()) {
      result.reason = "a transfer needs a human debit";
      return result;
    }
    std::string invalid;
    if (!EntriesAreSafe(humanDebit, invalid) ||
        !EntriesAreSafe(humanCredit, invalid)) {
      result.reason = invalid;
      return result;
    }
    if (PendingCount() >= kMaxPendingRecords) {
      result.reason = "pending merchant transfer cap is full";
      return result;
    }
    if (!TryLockPair(human.GetFormId(), merchant.GetFormId(), result.reason)) {
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

  Result CommitHumanDebit(MpActor& human, uint64_t offerId)
  {
    Result result;
    result.offerId = offerId;
    auto* offer = FindMutable(offerId);
    if (!offer || offer->state != State::Pending) {
      result.reason = "offer is not pending";
      return result;
    }
    if (offer->humanFormId != human.GetFormId()) {
      result.reason = "offer is for a different human";
      return result;
    }

    Inventory after = offer->expectedHumanInventory;
    if (!ApplyDelta(after, Inventory{}, offer->humanDebit, result.reason)) {
      return result;
    }
    if (!FitsSnapshot(after, result.reason)) {
      return result;
    }

    bool swapped = false;
    try {
      swapped = human.CompareAndSetInventory(offer->expectedHumanInventory,
                                              offer->expectedHumanReceipt,
                                              after,
                                              offer->expectedHumanSequence);
    } catch (const std::exception& error) {
      result.reason = error.what();
      return result;
    }
    if (!swapped) {
      result.reason = "human inventory or receipt did not match";
      return result;
    }

    offer->humanAfterDebit = after;
    offer->submittedReceipt = human.GetInventoryReceiptDump();
    offer->state = State::Quarantined;
    result.ok = true;
    return result;
  }

  bool IsHumanDebitDurable(MpActor& human, uint64_t offerId) const
  {
    auto offer = Find(offerId);
    if (!offer || offer->humanFormId != human.GetFormId()) {
      return false;
    }
    if (offer->state != State::Quarantined &&
        offer->state != State::Durable) {
      return false;
    }
    if (!savedReceipt || offer->submittedReceipt.empty()) {
      return false;
    }
    return savedReceipt(human) == offer->submittedReceipt;
  }

  Result ReleaseQuarantine(MpActor& human, MpObjectReference& merchant,
                           uint64_t offerId)
  {
    Result result;
    result.offerId = offerId;
    auto* offer = FindMutable(offerId);
    if (!offer || offer->state != State::Quarantined) {
      result.reason = "offer is not quarantined";
      return result;
    }
    if (offer->humanFormId != human.GetFormId() ||
        offer->merchantFormId != merchant.GetFormId()) {
      result.reason = "offer actors do not match";
      return result;
    }
    if (!savedReceipt) {
      result.reason = "saved receipt is not bound";
      return result;
    }
    if (savedReceipt(human) != offer->submittedReceipt) {
      result.reason = "human debit is not durable yet";
      return result;
    }

    if (offer->expectedHumanSequence >= kMaxSequence) {
      result.reason = "inventory receipt sequence must advance";
      return result;
    }
    const uint64_t creditSequence = offer->expectedHumanSequence + 1;

    Inventory humanNext = offer->humanAfterDebit;
    if (!offer->humanCredit.IsEmpty() &&
        !ApplyDelta(humanNext, offer->humanCredit, Inventory{},
                    result.reason)) {
      return result;
    }
    if (!FitsSnapshot(humanNext, result.reason)) {
      return result;
    }

    Inventory merchantBefore = merchant.GetInventory();
    Inventory merchantNext = merchantBefore;
    if (!ApplyDelta(merchantNext, offer->humanDebit, offer->humanCredit,
                    result.reason)) {
      return result;
    }
    if (!FitsSnapshot(merchantNext, result.reason)) {
      return result;
    }

    // Merchant first would pay out before the human credit is accepted.
    // Credit the human while the merchant snapshot is still staged, then
    // publish the merchant. On a failed human compare-and-set the merchant
    // inventory is left untouched.
    if (!offer->humanCredit.IsEmpty()) {
      bool credited = false;
      try {
        credited = human.CompareAndSetInventory(offer->humanAfterDebit,
                                                 offer->submittedReceipt,
                                                 humanNext, creditSequence);
      } catch (const std::exception& error) {
        result.reason = error.what();
        return result;
      }
      if (!credited) {
        result.reason = "human credit did not match the quarantined snapshot";
        return result;
      }
    }

    try {
      merchant.SetInventory(merchantNext);
    } catch (const std::exception& error) {
      result.reason = error.what();
      offer->reason = result.reason;
      return result;
    }

    offer->state = State::Durable;
    UnlockPair(offer->humanFormId, offer->merchantFormId);
    result.ok = true;
    return result;
  }

  Result Refuse(uint64_t offerId, const std::string& reason)
  {
    return Hold(offerId, State::Refused, reason);
  }

  Result MarkOffline(uint64_t offerId)
  {
    return Hold(offerId, State::Offline, "offline");
  }

  Result Forget(uint64_t offerId)
  {
    Result result;
    result.offerId = offerId;
    auto it = std::find_if(offers.begin(), offers.end(),
                           [&](const Offer& offer) { return offer.id == offerId; });
    if (it == offers.end()) {
      result.reason = "offer was not found";
      return result;
    }
    UnlockPair(it->humanFormId, it->merchantFormId);
    offers.erase(it);
    result.ok = true;
    return result;
  }

  bool IsLocked(uint32_t formId) const
  {
    return std::find(locks.begin(), locks.end(), formId) != locks.end();
  }

  uint32_t PendingCount() const
  {
    uint32_t count = 0;
    for (const auto& offer : offers) {
      if (CountsTowardCap(offer.state)) {
        ++count;
      }
    }
    return count;
  }

  std::optional<Offer> Find(uint64_t offerId) const
  {
    for (const auto& offer : offers) {
      if (offer.id == offerId) {
        return offer;
      }
    }
    return std::nullopt;
  }

private:
  static bool CountsTowardCap(State state)
  {
    return state == State::Pending || state == State::Quarantined ||
      state == State::Refused || state == State::Offline;
  }

  static bool EntriesAreSafe(const Inventory& inventory, std::string& reason)
  {
    for (const auto& entry : inventory.entries) {
      if (entry.baseId == 0 || entry.count == 0) {
        reason = "invalid transfer entry";
        return false;
      }
    }
    return FitsSnapshot(inventory, reason);
  }

  static bool FitsSnapshot(const Inventory& inventory, std::string& reason)
  {
    uint64_t total = 0;
    for (const auto& entry : inventory.entries) {
      total += entry.count;
      if (total > std::numeric_limits<uint32_t>::max()) {
        reason = "transfer count exceeds its limit";
        return false;
      }
    }
    if (inventory.ToJson().dump().size() > 128 * 1024) {
      reason = "transfer inventory exceeds 128 KiB";
      return false;
    }
    return true;
  }

  // Adds then removes on a copy of the rules, but removal is applied
  // first so a spent stack cannot fund itself. Metadata stays on the
  // matched entry. AddItems is not used: it returns after one merge.
  static bool ApplyDelta(Inventory& inventory, const Inventory& add,
                         const Inventory& remove, std::string& reason)
  {
    for (const auto& spent : remove.entries) {
      if (spent.baseId == 0 || spent.count == 0) {
        reason = "invalid transfer entry";
        return false;
      }
      uint32_t left = spent.count;
      for (auto& have : inventory.entries) {
        if (have.count == 0 || !have.EqualExceptCount(spent)) {
          continue;
        }
        uint32_t take = have.count < left ? have.count : left;
        have.count -= take;
        left -= take;
        if (left == 0) {
          break;
        }
      }
      if (left != 0) {
        reason = "not enough items for the transfer";
        return false;
      }
    }
    inventory.entries.erase(
      std::remove_if(inventory.entries.begin(), inventory.entries.end(),
                     [](const Inventory::Entry& entry) {
                       return entry.count == 0;
                     }),
      inventory.entries.end());

    for (const auto& gained : add.entries) {
      if (gained.baseId == 0 || gained.count == 0) {
        reason = "invalid transfer entry";
        return false;
      }
      bool merged = false;
      for (auto& have : inventory.entries) {
        if (!have.EqualExceptCount(gained)) {
          continue;
        }
        uint64_t sum =
          static_cast<uint64_t>(have.count) + gained.count;
        if (sum > std::numeric_limits<uint32_t>::max()) {
          reason = "transfer count overflows";
          return false;
        }
        have.count = static_cast<uint32_t>(sum);
        merged = true;
        break;
      }
      if (!merged) {
        inventory.entries.push_back(gained);
      }
    }
    return true;
  }

  bool TryLockPair(uint32_t humanFormId, uint32_t merchantFormId,
                   std::string& reason)
  {
    if (IsLocked(humanFormId) || IsLocked(merchantFormId)) {
      reason = "human or merchant is already in a transfer";
      return false;
    }
    locks.push_back(humanFormId);
    locks.push_back(merchantFormId);
    return true;
  }

  void UnlockPair(uint32_t humanFormId, uint32_t merchantFormId)
  {
    locks.erase(std::remove(locks.begin(), locks.end(), humanFormId),
                locks.end());
    locks.erase(std::remove(locks.begin(), locks.end(), merchantFormId),
                locks.end());
  }

  Offer* FindMutable(uint64_t offerId)
  {
    for (auto& offer : offers) {
      if (offer.id == offerId) {
        return &offer;
      }
    }
    return nullptr;
  }

  Result Hold(uint64_t offerId, State state, const std::string& reason)
  {
    Result result;
    result.offerId = offerId;
    auto* offer = FindMutable(offerId);
    if (!offer || (offer->state != State::Pending &&
                   offer->state != State::Quarantined)) {
      result.reason = "offer cannot be held";
      return result;
    }
    offer->state = state;
    offer->reason = reason;
    result.ok = true;
    result.reason = reason;
    return result;
  }

  WorldState& worldState;
  std::function<std::string(MpObjectReference&)> savedReceipt;
  std::vector<Offer> offers;
  std::vector<uint32_t> locks;
  uint64_t nextOfferId = 1;
};