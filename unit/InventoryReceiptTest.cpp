#include "TestUtils.hpp"
#include "WorldState.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <chrono>
#include <filesystem>
#include <limits>

extern espm::Loader& GetEspmLoader();

namespace {
constexpr uint32_t kHuman = 0xff000abc;

class ControlledFileStorage
  : public Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  std::filesystem::path root = std::filesystem::temp_directory_path() /
    ("thornswood-receipt-" +
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
    static const std::string name = "controlled-real-file";
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

Inventory InitialInventory()
{
  Inventory inv;
  inv.entries.emplace_back(0xf, 100);
  Inventory::ExtraData extras;
  extras.name = "Keepsake";
  extras.health = 0.8f;
  extras.chargePercent = 0.5f;
  extras.enchantmentId = 0x1234;
  extras.worn_ = false;
  inv.entries.emplace_back(0x12eb7, 2, extras);
  return inv;
}

void AttachSkyrimFiles(PartOne& server)
{
  // Created-actor FormDesc encoding and AttachSaveStorage FF checks need the
  // real plugin list. Empty espmFiles stores full ff ids and breaks save ack.
  server.AttachEspm(&GetEspmLoader());
}

MpActor& CreateHuman(PartOne& server)
{
  server.CreateActor(kHuman, { 1, 1, 1 }, 0, 0x3c, 42);
  auto& actor = server.worldState.GetFormAt<MpActor>(kHuman);
  actor.SetInventory(InitialInventory());
  return actor;
}
}

TEST_CASE("Inventory receipts compare both expected fields before mutation",
          "[inventory-receipt]")
{
  PartOne server;
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count = 90;
  auto stale = before;
  stale.entries[0].count = 80;
  REQUIRE_FALSE(actor.CompareAndSetInventory(stale, "null", after, 1));
  REQUIRE_FALSE(actor.CompareAndSetInventory(before, "{}", after, 1));
  REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
  REQUIRE(actor.GetInventoryReceiptDump() == "null");

  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1));
  REQUIRE(actor.GetInventory().ToJson() == after.ToJson());
  auto receipt = nlohmann::json::parse(actor.GetInventoryReceiptDump());
  REQUIRE(receipt.at("sequence") == 1);
  REQUIRE(receipt.at("profile") == 42);
  REQUIRE(receipt.at("actor") == actor.GetChangeForm().formDesc.ToString());
  REQUIRE(actor.GetChangeForm().dynamicFields.GetValueDump(
            MpObjectReference::kInventoryReceiptProperty) == receipt.dump());
  REQUIRE_FALSE(actor.CompareAndSetInventory(before, "null", after, 1));
}

TEST_CASE(
  "Receipt sequences and inventory limits refuse without partial changes",
  "[inventory-receipt]")
{
  PartOne server;
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count = 90;
  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1));
  auto receipt = actor.GetInventoryReceiptDump();
  auto next = after;
  next.entries[0].count = 80;
  REQUIRE_THROWS(actor.CompareAndSetInventory(after, receipt, next, 1));
  REQUIRE_THROWS(actor.CompareAndSetInventory(after, receipt, next, 0));
  REQUIRE_THROWS(actor.CompareAndSetInventory(after, receipt, next,
                                              uint64_t(9007199254740992)));
  auto oversized = next;
  oversized.entries[1].name = std::string(128 * 1024, 'a');
  REQUIRE_THROWS(actor.CompareAndSetInventory(after, receipt, oversized, 2));
  auto overflowing = next;
  overflowing.entries[0].count = std::numeric_limits<uint32_t>::max();
  REQUIRE_THROWS(actor.CompareAndSetInventory(after, receipt, overflowing, 2));
  REQUIRE(actor.GetInventory().ToJson() == after.ToJson());
  REQUIRE(actor.GetInventoryReceiptDump() == receipt);
  REQUIRE(actor.CompareAndSetInventory(after, receipt, next, 2));
  REQUIRE(actor.GetInventory().entries[1].name == "Keepsake");
  REQUIRE(actor.GetInventory().entries[1].enchantmentId == 0x1234);
}

TEST_CASE("Ordinary property setters cannot forge inventory receipts",
          "[inventory-receipt]")
{
  PartOne server;
  auto& actor = CreateHuman(server);
  REQUIRE_THROWS(
    actor.SetPropertyValueDump(MpObjectReference::kInventoryReceiptProperty,
                               "{\"sequence\":99}", true, true));
  REQUIRE(actor.GetInventoryReceiptDump() == "null");
  REQUIRE(actor.GetInventory().ToJson() == InitialInventory().ToJson());
}

TEST_CASE("Inventory receipt transactions refuse NPC profiles",
          "[inventory-receipt]")
{
  PartOne server;
  server.CreateActor(kHuman, { 1, 1, 1 }, 0, 0x3c);
  auto& actor = server.worldState.GetFormAt<MpActor>(kHuman);
  auto before = actor.GetInventory();
  REQUIRE_THROWS(actor.CompareAndSetInventory(before, "null", before, 1));
  REQUIRE(actor.GetInventoryReceiptDump() == "null");
  REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
}

TEST_CASE("Saved receipts acknowledge only the exact completed snapshot",
          "[inventory-receipt][espm]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto first = before;
  first.entries[0].count = 90;
  REQUIRE(actor.CompareAndSetInventory(before, "null", first, 1));
  auto firstReceipt = actor.GetInventoryReceiptDump();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == "null");
  server.Tick();
  REQUIRE(static_cast<bool>(storage->completion));

  auto second = first;
  second.entries[0].count = 80;
  REQUIRE(actor.CompareAndSetInventory(first, firstReceipt, second, 2));
  auto secondReceipt = actor.GetInventoryReceiptDump();
  storage->Commit();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == "null");
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == firstReceipt);
  REQUIRE(actor.GetInventoryReceiptDump() == secondReceipt);
  storage->Commit();
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == secondReceipt);
  REQUIRE_FALSE(static_cast<bool>(storage->completion));

  PartOne restarted;
  AttachSkyrimFiles(restarted);
  auto reloadedStorage = std::make_shared<
    Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>(
    std::make_shared<FileDatabase>(storage->root.string(),
                                   spdlog::default_logger()),
    spdlog::default_logger(), "file");
  restarted.AttachSaveStorage(reloadedStorage);
  auto& reloaded = restarted.worldState.GetFormAt<MpActor>(kHuman);
  REQUIRE(reloaded.GetInventory().ToJson() == second.ToJson());
  REQUIRE(restarted.worldState.GetSavedInventoryReceipt(reloaded) ==
          secondReceipt);
}

TEST_CASE("Failed saves do not advance receipt acknowledgement",
          "[inventory-receipt][espm]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count = 90;
  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1));
  auto receipt = actor.GetInventoryReceiptDump();
  server.Tick();
  storage->fail = true;
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == "null");
  REQUIRE(actor.GetInventoryReceiptDump() == receipt);
  REQUIRE(static_cast<bool>(storage->completion));
  storage->Commit();
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == receipt);
}