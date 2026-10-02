#include "TestUtils.hpp"
#include "WorldState.h"
#include "MerchantTransferController.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <chrono>
#include <filesystem>
#include <memory>

extern espm::Loader& GetEspmLoader();

namespace {
constexpr uint32_t kGold = 0xF;
constexpr uint32_t kGoods = 0x12eb7;

class ControlledFileStorage
  : public Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  std::filesystem::path root = std::filesystem::temp_directory_path() /
    ("thornswood-merchant-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  FileDatabase database{ root.string(), spdlog::default_logger() };
  std::vector<std::optional<MpChangeForm>> pending;
  UpsertCallback completion;
  bool ready = false;
  bool fail = false;
  uint32_t writes = 0;

  ~ControlledFileStorage() { std::filesystem::remove_all(root); }
  void IterateSync(const IterateSyncCallback& cb) override
  {
    database.Iterate(cb, std::nullopt);
  }
  void Upsert(std::vector<std::optional<MpChangeForm>>&& forms,
              const UpsertCallback& cb) override
  {
    REQUIRE_FALSE(static_cast<bool>(completion));
    pending = std::move(forms);
    completion = cb;
  }
  void Iterate(const IterateCallback& cb,
               const std::optional<std::vector<FormDesc>>& filter) override
  {
    std::vector<MpChangeForm> forms;
    database.Iterate([&](const MpChangeForm& form) { forms.push_back(form); },
                     filter);
    cb(forms);
  }
  uint32_t GetNumFinishedUpserts() const override { return writes; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  bool GetRecycledChangeFormsBuffer(
    std::vector<std::optional<MpChangeForm>>&) override
  {
    return false;
  }
  const std::string& GetName() const override
  {
    static const std::string name = "controlled-merchant-file";
    return name;
  }
  void Commit()
  {
    database.Upsert(std::vector<std::optional<MpChangeForm>>(pending));
    ready = true;
  }
  void Tick() override
  {
    if (fail) {
      fail = false;
      completion = {};
      throw Viet::AsyncSaveStorage<
        MpChangeForm, FormDesc,
        std::vector<FormDesc>>::UpsertFailedException(std::move(pending),
                                                      "injected failure");
    }
    if (ready) {
      ready = false;
      auto cb = std::move(completion);
      pending.clear();
      ++writes;
      cb();
    }
  }
};

Inventory Purse(uint32_t count)
{
  Inventory inventory;
  inventory.AddItem(kGold, count);
  return inventory;
}

Inventory Goods(uint32_t count)
{
  Inventory inventory;
  inventory.AddItem(kGoods, count);
  return inventory;
}

Inventory MerchantStock()
{
  Inventory inventory = Purse(250);
  inventory.AddItem(kGoods, 1);
  return inventory;
}

void AttachSkyrimFiles(PartOne& server)
{
  server.AttachEspm(&GetEspmLoader());
}

MpActor& AddHuman(PartOne& server, uint32_t formId)
{
  server.CreateActor(formId, { 1, 1, 1 }, 0, 0x3c, 42);
  auto& actor = server.worldState.GetFormAt<MpActor>(formId);
  actor.SetInventory(Purse(40));
  return actor;
}

MpActor& AddMerchant(PartOne& server, uint32_t formId)
{
  if (!server.HasEspm()) {
    AttachSkyrimFiles(server);
  }
  // Same gate the combat fixture uses so Hulda's placed NPC actually loads.
  server.worldState.npcEnabled = true;
  server.worldState.npcAllowEssential = true;
  server.worldState.npcAllowCrimeFaction = true;
  // Reuse Hulda's real base record; CreateActor's default base 0x7 is a player.
  auto hulda = server.worldState.LookupFormById(0x1a66e);
  REQUIRE(hulda);
  REQUIRE(hulda->AsActor());
  auto npc = std::make_unique<MpActor>(
    LocationalData{ { 2, 2, 2 }, { 0, 0, 0 },
                    FormDesc::FromFormId(0x3c, server.worldState.espmFiles) },
    server.CreateFormCallbacks(), hulda->AsActor()->GetBaseId());
  server.worldState.AddForm(std::move(npc), formId);
  auto& actor = server.worldState.GetFormAt<MpActor>(formId);
  actor.SetServerControlled(true);
  actor.SetInventory(MerchantStock());
  return actor;
}

MerchantTransferController::Result BeginTransfer(
  MerchantTransferController& transfers, MpActor& human, MpActor& merchant,
  uint32_t debitGold, uint32_t creditGoods, uint64_t sequence)
{
  Inventory credit = creditGoods == 0 ? Inventory{} : Goods(creditGoods);
  return transfers.Begin(human, merchant, Purse(debitGold), credit,
                         human.GetInventory(), human.GetInventoryReceiptDump(),
                         sequence);
}

void CompleteSave(PartOne& server, ControlledFileStorage& storage)
{
  server.Tick();
  REQUIRE(static_cast<bool>(storage.completion));
  storage.Commit();
  server.Tick();
  REQUIRE_FALSE(static_cast<bool>(storage.completion));
}

bool PendingHasForm(const ControlledFileStorage& storage, const FormDesc& desc)
{
  for (const auto& form : storage.pending) {
    if (form && form->formDesc == desc) {
      return true;
    }
  }
  return false;
}
}

TEST_CASE("HumanChangeUsesCompareAndSetInventoryReceiptPair",
          "[merchant-transfer]")
{
  PartOne server;
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto merchantBefore = merchant.GetInventory().ToJson();
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);

  auto committed = transfers.CommitHumanDebit(human, began.offerId);
  REQUIRE(committed.ok);
  REQUIRE(human.GetInventory().GetItemCount(kGold) == 30);
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 0);
  REQUIRE(merchant.GetInventory().ToJson() == merchantBefore);
  auto receipt = nlohmann::json::parse(human.GetInventoryReceiptDump());
  REQUIRE(receipt.at("sequence") == 1);
  REQUIRE(receipt.at("profile") == 42);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Quarantined);
  REQUIRE(offer->submittedReceipt == human.GetInventoryReceiptDump());
}

TEST_CASE("HumanSuccessAckMatchesGetSavedInventoryReceipt",
          "[merchant-transfer]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  auto submitted = human.GetInventoryReceiptDump();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(human) == "null");
  REQUIRE_FALSE(transfers.IsHumanDebitDurable(human, began.offerId));

  CompleteSave(server, *storage);
  REQUIRE(server.worldState.GetSavedInventoryReceipt(human) == submitted);
  REQUIRE(transfers.IsHumanDebitDurable(human, began.offerId));
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->submittedReceipt == submitted);
}

TEST_CASE("FailedHumanWriteLeavesAckUnchangedAndRequeuesForm",
          "[merchant-transfer]")
{
  // Re-queue is the storage completion left set. The controller has no requeue method.
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto merchantBefore = merchant.GetInventory().ToJson();
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  auto liveReceipt = human.GetInventoryReceiptDump();
  server.Tick();
  storage->fail = true;
  server.Tick();

  REQUIRE(server.worldState.GetSavedInventoryReceipt(human) == "null");
  REQUIRE(human.GetInventoryReceiptDump() == liveReceipt);
  REQUIRE_FALSE(transfers.IsHumanDebitDurable(human, began.offerId));
  REQUIRE(static_cast<bool>(storage->completion));
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 0);
  REQUIRE(merchant.GetInventory().ToJson() == merchantBefore);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Quarantined);
}

TEST_CASE("TransferLocksHumanAndMerchantTogether", "[merchant-transfer]")
{
  PartOne server;
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 1, 0, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.IsLocked(human.GetFormId()));
  REQUIRE(transfers.IsLocked(merchant.GetFormId()));
  REQUIRE(transfers.PendingCount() == 1);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Pending);
  REQUIRE(offer->humanFormId == human.GetFormId());
  REQUIRE(offer->merchantFormId == merchant.GetFormId());
}

TEST_CASE("SecondTransferOnEitherActorWaitsOrRefuses", "[merchant-transfer]")
{
  // Begin has no wait result. A second offer on either locked actor refuses.
  PartOne server;
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& otherHuman = AddHuman(server, 0xff000ab1);
  auto& otherMerchant = AddMerchant(server, 0xff000de1);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 1, 0, 1);
  REQUIRE(began.ok);
  auto sameHuman = BeginTransfer(transfers, human, otherMerchant, 1, 0, 1);
  REQUIRE_FALSE(sameHuman.ok);
  auto sameMerchant = BeginTransfer(transfers, otherHuman, merchant, 1, 0, 1);
  REQUIRE_FALSE(sameMerchant.ok);
  REQUIRE(transfers.PendingCount() == 1);
  REQUIRE(transfers.IsLocked(human.GetFormId()));
  REQUIRE(transfers.IsLocked(merchant.GetFormId()));
  REQUIRE_FALSE(transfers.Find(sameHuman.offerId));
  REQUIRE_FALSE(transfers.Find(sameMerchant.offerId));
}

TEST_CASE("UnlockOnEveryFailureIncludingThrownSave", "[merchant-transfer]")
{
  // Header has no unlock-on-throw method. Commit catches the error, and Forget drops locks.
  PartOne server;
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto humanBefore = human.GetInventory().ToJson();
  auto merchantBefore = merchant.GetInventory().ToJson();
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 0);
  REQUIRE(began.ok);
  REQUIRE(transfers.IsLocked(human.GetFormId()));
  auto committed = transfers.CommitHumanDebit(human, began.offerId);
  REQUIRE_FALSE(committed.ok);
  REQUIRE(human.GetInventory().ToJson() == humanBefore);
  REQUIRE(merchant.GetInventory().ToJson() == merchantBefore);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Pending);
  REQUIRE(transfers.Forget(began.offerId).ok);
  REQUIRE_FALSE(transfers.IsLocked(human.GetFormId()));
  REQUIRE_FALSE(transfers.IsLocked(merchant.GetFormId()));
  REQUIRE_FALSE(transfers.Find(began.offerId));

  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto again = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(again.ok);
  REQUIRE(transfers.CommitHumanDebit(human, again.offerId).ok);
  server.Tick();
  storage->fail = true;
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(human) == "null");
  REQUIRE_FALSE(transfers.IsHumanDebitDurable(human, again.offerId));
  REQUIRE(merchant.GetInventory().ToJson() == merchantBefore);
  REQUIRE(transfers.IsLocked(human.GetFormId()));
  REQUIRE(transfers.Forget(again.offerId).ok);
  REQUIRE_FALSE(transfers.IsLocked(human.GetFormId()));
  REQUIRE_FALSE(transfers.IsLocked(merchant.GetFormId()));
}

TEST_CASE("IncomingCreditsStayQuarantinedUntilMatchingDebitIsDurable",
          "[merchant-transfer]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  REQUIRE(human.GetInventory().GetItemCount(kGold) == 30);
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 0);
  REQUIRE(merchant.GetInventory().GetItemCount(kGoods) == 1);
  REQUIRE(merchant.GetInventory().GetItemCount(kGold) == 250);
  auto early = transfers.ReleaseQuarantine(human, merchant, began.offerId);
  REQUIRE_FALSE(early.ok);
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 0);
  REQUIRE(merchant.GetInventory().GetItemCount(kGoods) == 1);

  CompleteSave(server, *storage);
  REQUIRE(transfers.IsHumanDebitDurable(human, began.offerId));
  auto released = transfers.ReleaseQuarantine(human, merchant, began.offerId);
  REQUIRE(released.ok);
  REQUIRE(human.GetInventory().GetItemCount(kGold) == 30);
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 1);
  REQUIRE(merchant.GetInventory().GetItemCount(kGold) == 260);
  REQUIRE(merchant.GetInventory().GetItemCount(kGoods) == 0);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Durable);
}

TEST_CASE("FailedOrPartialWriteDoesNotCreditPayeeWhilePayerUnchanged",
          "[merchant-transfer]")
{
  PartOne server;
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto humanBefore = human.GetInventory().ToJson();
  auto merchantBefore = merchant.GetInventory().ToJson();
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 100, 1, 1);
  REQUIRE(began.ok);
  auto committed = transfers.CommitHumanDebit(human, began.offerId);
  REQUIRE_FALSE(committed.ok);
  REQUIRE(human.GetInventory().ToJson() == humanBefore);
  REQUIRE(merchant.GetInventory().ToJson() == merchantBefore);
  REQUIRE(human.GetInventory().GetItemCount(kGoods) == 0);
  REQUIRE(merchant.GetInventory().GetItemCount(kGold) == 250);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Pending);
}

TEST_CASE("MerchantChangeJoinsSameFileDatabaseBatchUpsertAsHuman",
          "[merchant-transfer]")
{
  // Batch Upsert is not a controller method. Both forms show up in the controlled storage pending vector.
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  CompleteSave(server, *storage);
  REQUIRE(transfers.ReleaseQuarantine(human, merchant, began.offerId).ok);
  server.Tick();
  REQUIRE(static_cast<bool>(storage->completion));
  REQUIRE(PendingHasForm(*storage, human.GetChangeForm().formDesc));
  REQUIRE(PendingHasForm(*storage, merchant.GetChangeForm().formDesc));
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Durable);
}

TEST_CASE("MerchantDurabilityAckIsBatchUpsertCompletionNotInventoryReceipt",
          "[merchant-transfer]")
{
  // Header has no merchant receipt stamp. SetInventory publishes the merchant, and the receipt dump stays null.
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  CompleteSave(server, *storage);
  REQUIRE(transfers.ReleaseQuarantine(human, merchant, began.offerId).ok);
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");
  REQUIRE(merchant.GetChangeForm().dynamicFields.GetValueDump(
            MpObjectReference::kInventoryReceiptProperty) == "null");
  REQUIRE(human.GetInventoryReceiptDump() != "null");
  REQUIRE_FALSE(transfers.IsLocked(merchant.GetFormId()));
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Durable);
}

TEST_CASE("MerchantPathDoesNotCallCompareAndSetInventory",
          "[merchant-transfer]")
{
  PartOne server;
  auto& merchant = AddMerchant(server, 0xff000def);
  auto before = merchant.GetInventory();
  REQUIRE_THROWS(merchant.CompareAndSetInventory(before, "null", before, 1));
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");

  auto& human = AddHuman(server, 0xff000abc);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  REQUIRE(merchant.GetInventory().ToJson() == before.ToJson());
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");
  REQUIRE(human.GetInventory().GetItemCount(kGold) == 30);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Quarantined);
}

TEST_CASE("ControllerDoesNotTreatNpcCompareAndSetInventoryAsSuccess",
          "[merchant-transfer]")
{
  PartOne server;
  auto& merchant = AddMerchant(server, 0xff000def);
  auto before = merchant.GetInventory();
  REQUIRE_THROWS(merchant.CompareAndSetInventory(before, "null", before, 1));
  REQUIRE(merchant.GetInventory().ToJson() == before.ToJson());
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");

  auto& human = AddHuman(server, 0xff000abc);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Pending);
  auto released = transfers.ReleaseQuarantine(human, merchant, began.offerId);
  REQUIRE_FALSE(released.ok);
  REQUIRE(merchant.GetInventory().ToJson() == before.ToJson());
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");
  offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Pending);
}

TEST_CASE("MerchantPathDoesNotSetInventoryReceiptViaSetPropertyValueDump",
          "[merchant-transfer]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  REQUIRE_THROWS(merchant.SetPropertyValueDump(
    MpObjectReference::kInventoryReceiptProperty, "{\"sequence\":99}", true,
    true));
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");

  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  CompleteSave(server, *storage);
  REQUIRE(transfers.ReleaseQuarantine(human, merchant, began.offerId).ok);
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");
  REQUIRE(merchant.GetInventory().GetItemCount(kGold) == 260);
  REQUIRE(human.GetInventoryReceiptDump() != "null");
}

TEST_CASE("MerchantAckStampIsNotInventoryReceiptProperty",
          "[merchant-transfer]")
{
  // Header stores no merchant ack field. A finished offer still leaves kInventoryReceiptProperty null on the NPC.
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& human = AddHuman(server, 0xff000abc);
  auto& merchant = AddMerchant(server, 0xff000def);
  auto& transfers = server.worldState.GetMerchantTransfers();
  auto began = BeginTransfer(transfers, human, merchant, 10, 1, 1);
  REQUIRE(began.ok);
  REQUIRE(transfers.CommitHumanDebit(human, began.offerId).ok);
  CompleteSave(server, *storage);
  REQUIRE(transfers.ReleaseQuarantine(human, merchant, began.offerId).ok);
  CompleteSave(server, *storage);
  REQUIRE(merchant.GetInventoryReceiptDump() == "null");
  REQUIRE(merchant.GetChangeForm().dynamicFields.GetValueDump(
            MpObjectReference::kInventoryReceiptProperty) == "null");
  REQUIRE(server.worldState.GetSavedInventoryReceipt(merchant) == "null");
  REQUIRE(human.GetInventoryReceiptDump() != "null");
  auto offer = transfers.Find(began.offerId);
  REQUIRE(offer);
  REQUIRE(offer->state == MerchantTransferController::State::Durable);
  REQUIRE(offer->submittedReceipt != "null");
  REQUIRE(offer->submittedReceipt != merchant.GetInventoryReceiptDump());
}

TEST_CASE("PendingCapIs25IncludingOfflineAndRefused", "[merchant-transfer]")
{
  PartOne server;
  auto& transfers = server.worldState.GetMerchantTransfers();
  REQUIRE(MerchantTransferController::kMaxPendingRecords == 25);
  uint64_t refusedId = 0;
  uint64_t offlineId = 0;
  uint64_t pendingId = 0;
  for (uint32_t i = 0; i < MerchantTransferController::kMaxPendingRecords;
       ++i) {
    auto& human = AddHuman(server, 0xff100000 + i);
    auto& merchant = AddMerchant(server, 0xff200000 + i);
    auto began = BeginTransfer(transfers, human, merchant, 1, 0, 1);
    REQUIRE(began.ok);
    if (i % 3 == 1) {
      REQUIRE(transfers.Refuse(began.offerId, "refused").ok);
      refusedId = began.offerId;
    } else if (i % 3 == 2) {
      REQUIRE(transfers.MarkOffline(began.offerId).ok);
      offlineId = began.offerId;
    } else {
      pendingId = began.offerId;
    }
  }
  REQUIRE(transfers.PendingCount() == 25);
  REQUIRE(transfers.Find(refusedId)->state ==
          MerchantTransferController::State::Refused);
  REQUIRE(transfers.Find(offlineId)->state ==
          MerchantTransferController::State::Offline);
  REQUIRE(transfers.Find(pendingId)->state ==
          MerchantTransferController::State::Pending);
}

TEST_CASE("TwentySixthOfferRefusesWithoutMutatingPursesOrStock",
          "[merchant-transfer]")
{
  PartOne server;
  auto& transfers = server.worldState.GetMerchantTransfers();
  for (uint32_t i = 0; i < MerchantTransferController::kMaxPendingRecords;
       ++i) {
    auto& human = AddHuman(server, 0xff110000 + i);
    auto& merchant = AddMerchant(server, 0xff210000 + i);
    auto began = BeginTransfer(transfers, human, merchant, 1, 0, 1);
    REQUIRE(began.ok);
    if (i % 2 == 0) {
      REQUIRE(transfers.Refuse(began.offerId, "refused").ok);
    } else {
      REQUIRE(transfers.MarkOffline(began.offerId).ok);
    }
  }
  auto& extraHuman = AddHuman(server, 0xff110100);
  auto& extraMerchant = AddMerchant(server, 0xff210100);
  auto humanBefore = extraHuman.GetInventory().ToJson();
  auto merchantBefore = extraMerchant.GetInventory().ToJson();
  auto blocked = BeginTransfer(transfers, extraHuman, extraMerchant, 1, 0, 1);
  REQUIRE_FALSE(blocked.ok);
  REQUIRE(transfers.PendingCount() == 25);
  REQUIRE(extraHuman.GetInventory().ToJson() == humanBefore);
  REQUIRE(extraMerchant.GetInventory().ToJson() == merchantBefore);
  REQUIRE(extraHuman.GetInventory().GetItemCount(kGold) == 40);
  REQUIRE(extraMerchant.GetInventory().GetItemCount(kGold) == 250);
  REQUIRE(extraMerchant.GetInventory().GetItemCount(kGoods) == 1);
  REQUIRE_FALSE(transfers.IsLocked(extraHuman.GetFormId()));
  REQUIRE_FALSE(transfers.IsLocked(extraMerchant.GetFormId()));
}

TEST_CASE("RefusedOfferStaysPendingAndStillCountsTowardCapUntilDropped",
          "[merchant-transfer]")
{
  PartOne server;
  auto& transfers = server.worldState.GetMerchantTransfers();
  uint64_t keptId = 0;
  uint32_t keptHuman = 0;
  uint64_t droppedId = 0;
  uint32_t droppedHuman = 0;
  uint32_t droppedMerchant = 0;
  for (uint32_t i = 0; i < MerchantTransferController::kMaxPendingRecords;
       ++i) {
    uint32_t humanId = 0xff120000 + i;
    uint32_t merchantId = 0xff220000 + i;
    auto& human = AddHuman(server, humanId);
    auto& merchant = AddMerchant(server, merchantId);
    auto began = BeginTransfer(transfers, human, merchant, 1, 0, 1);
    REQUIRE(began.ok);
    REQUIRE(transfers.Refuse(began.offerId, "refused").ok);
    if (i == 0) {
      droppedId = began.offerId;
      droppedHuman = humanId;
      droppedMerchant = merchantId;
    } else {
      keptId = began.offerId;
      keptHuman = humanId;
    }
  }
  REQUIRE(transfers.PendingCount() == 25);
  REQUIRE(transfers.Forget(droppedId).ok);
  REQUIRE_FALSE(transfers.Find(droppedId));
  REQUIRE_FALSE(transfers.IsLocked(droppedHuman));
  REQUIRE_FALSE(transfers.IsLocked(droppedMerchant));
  REQUIRE(transfers.PendingCount() == 24);
  REQUIRE(transfers.Find(keptId)->state ==
          MerchantTransferController::State::Refused);
  REQUIRE(transfers.IsLocked(keptHuman));

  auto& human = AddHuman(server, 0xff120100);
  auto& merchant = AddMerchant(server, 0xff220100);
  auto again = BeginTransfer(transfers, human, merchant, 1, 0, 1);
  REQUIRE(again.ok);
  REQUIRE(transfers.PendingCount() == 25);
  auto overflow = BeginTransfer(transfers, AddHuman(server, 0xff120101),
                                AddMerchant(server, 0xff220101), 1, 0, 1);
  REQUIRE_FALSE(overflow.ok);
  REQUIRE(transfers.PendingCount() == 25);
  REQUIRE(transfers.Find(keptId)->state ==
          MerchantTransferController::State::Refused);
}
