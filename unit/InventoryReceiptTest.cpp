#include "TestUtils.hpp"
#include "WorldState.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>

// RequestPreparedSave prepares the two optional map destinations separately.
// Every other nontrivial snapshot member must stay nonthrowing on assignment.
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::formDesc)>, "formDesc");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::baseDesc)>, "baseDesc");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::position)>, "position");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::angle)>, "angle");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::worldOrCellDesc)>, "worldOrCellDesc");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::inv)>, "inv");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::learnedSpells)>, "learnedSpells");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::activeMagicEffects)>, "activeMagicEffects");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::appearanceDump)>, "appearanceDump");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::equipment)>, "equipment");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::actorValues)>, "actorValues");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::spawnPoint)>, "spawnPoint");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::templateChain)>, "templateChain");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::lastAnimation)>, "lastAnimation");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::displayName)>, "displayName");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::factions)>, "factions");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::quests)>, "quests");
static_assert(std::is_nothrow_move_assignable_v<decltype(MpChangeForm::dynamicFields)>, "dynamicFields");

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

TEST_CASE("Inventory and quest state compare and commit together",
          "[inventory-receipt]")
{
  PartOne server;
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count += 25;
  using Change = MpObjectReference::InventoryPropertyChange;
  Change change{ "thornswoodQuests", "null",
                 R"({"v":1,"open":{},"cool":{"wolf":123456}})" };
  auto wrong = change;
  wrong.expectedDump = "{}";
  REQUIRE_FALSE(actor.CompareAndSetInventory(before, "null", after, 1, &wrong));
  REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
  REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == "null");
  REQUIRE(actor.GetInventoryReceiptDump() == "null");

  for (const auto& invalid : { std::string("{"),
                               std::string(16 * 1024 + 1, ' ') }) {
    wrong = change;
    wrong.replacementDump = invalid;
    REQUIRE_THROWS(actor.CompareAndSetInventory(before, "null", after, 1, &wrong));
    REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
    REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == "null");
    REQUIRE(actor.GetInventoryReceiptDump() == "null");
  }
  wrong = change;
  wrong.name = MpObjectReference::kInventoryReceiptProperty;
  REQUIRE_THROWS(actor.CompareAndSetInventory(before, "null", after, 1, &wrong));

  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1, &change));
  REQUIRE(actor.GetInventory().ToJson() == after.ToJson());
  REQUIRE(nlohmann::json::parse(actor.GetDynamicFields().GetValueDump(change.name)) ==
          nlohmann::json::parse(change.replacementDump));
  auto receipt = actor.GetInventoryReceiptDump();
  REQUIRE_FALSE(actor.CompareAndSetInventory(after, receipt, before, 2, &change));
  REQUIRE(actor.GetInventoryReceiptDump() == receipt);

  // Reload canonicalizes key order. Comparison must retain JSON semantics.
  change.expectedDump = R"({"cool":{"wolf":123456},"open":{},"v":1})";
  change.replacementDump = R"({"v":1,"open":{"ebony":{"drawn":0}},"cool":{"wolf":123456}})";
  REQUIRE(actor.CompareAndSetInventory(after, receipt, after, 2, &change));
  REQUIRE(actor.GetInventory().entries[1].name == "Keepsake");
  REQUIRE(actor.GetInventory().entries[1].enchantmentId == 0x1234);
}

TEST_CASE("A stale runtime owner cannot commit quest goods or their receipt",
          "[inventory-receipt][actor-runtime-identity]")
{
  PartOne server;
  auto& actor = CreateHuman(server);
  const auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count += 25;
  ActorsMap owners;
  owners.Set(0, &actor);
  MpObjectReference::InventoryPropertyChange change{
    "thornswoodQuests", "null", "{}" };
  change.expectedRuntimeIdentity = actor.GetRuntimeIdentity();
  owners.Erase(static_cast<Networking::UserId>(0));
  REQUIRE_FALSE(actor.CompareAndSetInventory(before, "null", after, 1, &change));
  REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
  REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == "null");
  REQUIRE(actor.GetInventoryReceiptDump() == "null");
  change.expectedRuntimeIdentity = actor.GetRuntimeIdentity();
  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1, &change));
  REQUIRE(actor.GetInventory().ToJson() == after.ToJson());
  REQUIRE(actor.GetInventoryReceiptDump() != "null");
}

TEST_CASE("Saved quest receipts acknowledge the same inventory and property",
          "[inventory-receipt][espm]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory();
  auto after = before;
  after.entries[0].count += 25;
  MpObjectReference::InventoryPropertyChange change{
    "thornswoodQuests", "null",
    R"({"v":1,"open":{},"cool":{"wolf":123456}})"
  };
  REQUIRE(actor.CompareAndSetInventory(before, "null", after, 1, &change));
  const auto receipt = actor.GetInventoryReceiptDump();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == "null");
  server.Tick();
  REQUIRE(static_cast<bool>(storage->completion));
  auto submitted = std::find_if(storage->pending.begin(), storage->pending.end(),
    [](const auto& form) { return form && form->profileId == 42; });
  REQUIRE(submitted != storage->pending.end());
  REQUIRE((*submitted)->inv.ToJson() == after.ToJson());
  REQUIRE(nlohmann::json::parse((*submitted)->dynamicFields.GetValueDump(change.name)) ==
          nlohmann::json::parse(change.replacementDump));
  REQUIRE((*submitted)->dynamicFields.GetValueDump(MpObjectReference::kInventoryReceiptProperty) == receipt);
  storage->Commit();
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == receipt);

  PartOne restarted;
  AttachSkyrimFiles(restarted);
  auto reloadedStorage = std::make_shared<
    Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>(
    std::make_shared<FileDatabase>(storage->root.string(), spdlog::default_logger()),
    spdlog::default_logger(), "file");
  restarted.AttachSaveStorage(reloadedStorage);
  auto& reloaded = restarted.worldState.GetFormAt<MpActor>(kHuman);
  REQUIRE(reloaded.GetInventory().ToJson() == after.ToJson());
  REQUIRE(nlohmann::json::parse(reloaded.GetDynamicFields().GetValueDump(change.name)) ==
          nlohmann::json::parse(change.replacementDump));
  REQUIRE(restarted.worldState.GetSavedInventoryReceipt(reloaded) == receipt);
}

TEST_CASE("Quest inventory refuses replaced lives and normalized oversized properties",
          "[inventory-receipt][espm]")
{
  PartOne server;
  AttachSkyrimFiles(server);
  auto& actor = CreateHuman(server);
  const auto before = actor.GetInventory();
  MpObjectReference::InventoryPropertyChange change{ "thornswoodQuests", "null", "{}" };
  actor.SetPropertyValueDump("_skympNpcLifeGeneration", "1", false, false);
  REQUIRE_FALSE(actor.CompareAndSetInventory(before, "null", before, 1, &change));
  actor.SetPropertyValueDump("_skympNpcLifeGeneration", "0", false, false);
  change.replacementDump = "[";
  for (int i = 0; i < 3000; ++i) change.replacementDump += i ? ",1e-7" : "1e-7";
  change.replacementDump += "]";
  REQUIRE(change.replacementDump.size() < 16 * 1024);
  REQUIRE_THROWS(actor.CompareAndSetInventory(before, "null", before, 1, &change));
  REQUIRE(actor.GetInventory().ToJson() == before.ToJson());
  REQUIRE(actor.GetInventoryReceiptDump() == "null");
  REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == "null");
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
TEST_CASE("Metadata CAS stages its save before changing live progress",
          "[inventory-receipt][actor-metadata]")
{
  struct Actor : MpActor {
    using MpActor::MpActor;
    bool failSnapshot = false;
    void SuspendSaving() { blockSaving = true; }
    MpChangeForm GetChangeForm() const override {
      if (failSnapshot) throw std::bad_alloc();
      return MpActor::GetChangeForm();
    }
  };
  PartOne server;
  auto created = std::make_unique<Actor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()},
    server.CreateFormCallbacks(), 0x7);
  auto& actor = *created;
  server.worldState.AddForm(std::move(created), kHuman);
  actor.RegisterProfileId(42);
  actor.SetInventory(InitialInventory());
  MpObjectReference::InventoryPropertyChange change{"thornswoodQuests", "null", "{\"kills\":1}"};
  change.expectedRuntimeIdentity = actor.GetRuntimeIdentity();
  SECTION("Snapshot allocation fails before live metadata changes") {
    actor.failSnapshot = true;
    REQUIRE_THROWS_AS(actor.CompareAndSetActorProperty(42, change), std::bad_alloc);
  }
  SECTION("Suspended saving refuses instead of reporting a queued commit") {
    actor.SuspendSaving();
    REQUIRE_FALSE(actor.CompareAndSetActorProperty(42, change));
  }
  REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == "null");
  REQUIRE(actor.GetInventoryReceiptDump() == "null");
  REQUIRE(actor.GetInventory().ToJson() == InitialInventory().ToJson());
}

TEST_CASE("Metadata saves preserve in-flight goods and later inventory receipts",
          "[inventory-receipt][actor-metadata][espm]")
{
  const bool failFirst = GENERATE(false, true);
  PartOne server;
  AttachSkyrimFiles(server);
  auto storage = std::make_shared<ControlledFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = CreateHuman(server);
  auto before = actor.GetInventory(), debit = before;
  debit.entries[0].count = 90;
  REQUIRE(actor.CompareAndSetInventory(before, "null", debit, 1));
  const auto debitReceipt = actor.GetInventoryReceiptDump();
  server.Tick();
  REQUIRE(static_cast<bool>(storage->completion));
  MpObjectReference::InventoryPropertyChange change{"thornswoodQuests", "null", ""};
  change.expectedRuntimeIdentity = actor.GetRuntimeIdentity();
  for (int kills = 1; kills <= 3; ++kills) {
    change.replacementDump = nlohmann::json{{"kills", kills}}.dump();
    REQUIRE(actor.CompareAndSetActorProperty(42, change));
    change.expectedDump = change.replacementDump;
    REQUIRE(actor.GetInventoryReceiptDump() == debitReceipt);
    REQUIRE(actor.GetInventory().ToJson() == debit.ToJson());
    REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == "null");
  }
  auto credit = debit;
  credit.entries[0].count += 25;
  REQUIRE(actor.CompareAndSetInventory(debit, debitReceipt, credit, 2));
  const auto creditReceipt = actor.GetInventoryReceiptDump();
  if (failFirst) storage->fail = true;
  else storage->Commit();
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == (failFirst ? "null" : debitReceipt));
  REQUIRE(actor.GetInventoryReceiptDump() == creditReceipt);
  REQUIRE(actor.GetDynamicFields().GetValueDump(change.name) == change.replacementDump);
  REQUIRE(static_cast<bool>(storage->completion));
  const auto submitted = std::find_if(storage->pending.begin(), storage->pending.end(),
    [](const auto& form) { return form && form->profileId == 42; });
  REQUIRE(submitted != storage->pending.end());
  REQUIRE((*submitted)->inv.ToJson() == credit.ToJson());
  REQUIRE((*submitted)->dynamicFields.GetValueDump(change.name) == change.replacementDump);
  storage->Commit();
  server.Tick();
  REQUIRE(server.worldState.GetSavedInventoryReceipt(actor) == creditReceipt);
  std::vector<MpChangeForm> saved;
  storage->database.Iterate([&](const MpChangeForm& form) { if (form.profileId == 42) saved.push_back(form); }, std::nullopt);
  REQUIRE(saved.size() == 1);
  REQUIRE(saved[0].inv.ToJson() == credit.ToJson());
  REQUIRE(saved[0].dynamicFields.GetValueDump(change.name) == change.replacementDump);
  REQUIRE(saved[0].dynamicFields.GetValueDump(MpObjectReference::kInventoryReceiptProperty) == creditReceipt);
}

TEST_CASE("Transaction preflight uses the exact native serialized byte bound",
          "[inventory-receipt]")
{
  Inventory snapshot;
  snapshot.AddItem(0x5ace4, 1);
  snapshot.entries[0].health = 1.0f;
  snapshot.entries[0].name = "";
  const auto overhead = snapshot.ToJson().dump().size();
  snapshot.entries[0].name = std::string(128 * 1024 - overhead, 'x');
  REQUIRE(snapshot.ToJson().dump().size() == 128 * 1024);
  REQUIRE_NOTHROW(MpObjectReference::ValidateTransactionInventory(snapshot));
  snapshot.entries[0].name.value() += "x";
  REQUIRE_THROWS(MpObjectReference::ValidateTransactionInventory(snapshot));
}
