#include "TestUtils.hpp"
#include "WorldState.h"
#include "PendingSaveBatch.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/ISaveStorage.h"
#include "save_storages/AsyncSaveStorage.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

extern espm::Loader& GetEspmLoader();

namespace {
class CallbackFileStorage
  : public Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  std::filesystem::path root = std::filesystem::temp_directory_path() /
    ("thornswood-save-pump-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  FileDatabase database{root.string(), spdlog::default_logger()};
  std::function<void()> onSubmit, onRecycle, onTick;
  UpsertCallback deferred;
  bool defer = false, failSubmission = false;
  bool failBeforeConsumption = false;
  bool throwNonstandard = false;
  uint32_t writes = 0;

  ~CallbackFileStorage() { std::filesystem::remove_all(root); }
  void IterateSync(const IterateSyncCallback& cb) override
  { database.Iterate(cb, std::nullopt); }
  void Iterate(const IterateCallback& cb,
               const std::optional<std::vector<FormDesc>>& filter) override
  {
    std::vector<MpChangeForm> rows;
    database.Iterate([&](const MpChangeForm& row) { rows.push_back(row); }, filter);
    cb(rows);
  }
  static void RunOnce(std::function<void()>& hook)
  {
    auto once = std::move(hook);
    hook = {};
    if (once) once();
  }
  void Upsert(std::vector<std::optional<MpChangeForm>>&& rows,
              const UpsertCallback& cb) override
  {
    if (failBeforeConsumption) {
      failBeforeConsumption = false;
      RunOnce(onSubmit);
      if (throwNonstandard) throw 17;
      throw std::runtime_error("unconsumed submission failed");
    }
    auto submitted = std::move(rows);
    if (failSubmission) {
      failSubmission = false;
      RunOnce(onSubmit);
      throw std::runtime_error("consumed submission failed");
    }
    database.Upsert(std::move(submitted));
    ++writes;
    if (defer) deferred = cb;
    else cb();
    RunOnce(onSubmit);
  }
  void Tick() override { RunOnce(onTick); }
  bool GetRecycledChangeFormsBuffer(
    std::vector<std::optional<MpChangeForm>>& rows) override
  {
    RunOnce(onRecycle);
    rows.assign(64, std::nullopt);
    return true;
  }
  uint32_t GetNumFinishedUpserts() const override { return writes; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  const std::string& GetName() const override
  { static const std::string name = "callback-real-file"; return name; }
  MpChangeForm Read(const MpActor& actor)
  {
    auto saved = Viet::ISaveStorageUtils::FindSync(*this, actor.GetChangeForm().formDesc);
    REQUIRE(saved.has_value());
    return *saved;
  }
};

MpActor& AddHuman(PartOne& server, uint32_t id = 0xff000abc)
{
  server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
  return server.worldState.GetFormAt<MpActor>(id);
}

void SetProgress(MpActor& actor, const std::string& value)
{
  actor.SetPropertyValueDump("thornswoodQuests", value, false, false);
}
}

TEST_CASE("Inline save callbacks preserve newer metadata", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  const auto receipt = actor.GetInventoryReceiptDump();
  SetProgress(actor, "1");
  storage->onSubmit = [&] { SetProgress(actor, "2"); };
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "1");
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "2");
  REQUIRE(actor.GetInventoryReceiptDump() == receipt);
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump(
    MpObjectReference::kInventoryReceiptProperty) == receipt);
}

TEST_CASE("Recycling a save buffer preserves callback work", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  storage->onRecycle = [&] { SetProgress(actor, "2"); };
  server.worldState.Tick();
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "2");
}

TEST_CASE("Save callbacks cannot replace storage or tick gameplay", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  auto replacement = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  AddHuman(server);
  bool timerFired = false, hookCalled = false;
  server.worldState.SetTimer(std::chrono::milliseconds(0)).Then(
    [&](Viet::Void) { timerFired = true; });
  auto check = [&] {
    hookCalled = true;
    CHECK_THROWS_AS(server.worldState.AttachSaveStorage(nullptr), std::runtime_error);
    CHECK_THROWS_AS(server.worldState.AttachSaveStorage(replacement), std::runtime_error);
    CHECK_THROWS_AS(server.worldState.Tick(), std::runtime_error);
    CHECK_FALSE(timerFired);
  };
  SECTION("storage tick") { storage->onTick = check; }
  SECTION("inline submission") { storage->onSubmit = check; }
  SECTION("buffer recycle") { storage->onRecycle = check; }
  server.worldState.Tick();
  REQUIRE(hookCalled);
  REQUIRE(timerFired);
  REQUIRE(storage->writes == 1);
  REQUIRE(replacement->writes == 0);
  server.worldState.AttachSaveStorage(replacement);
}

TEST_CASE("Queued or in-flight saves retain their backend", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto& actor = AddHuman(server);
  auto storage = std::make_shared<CallbackFileStorage>();
  // Startup may attach its first backend after forms have queued saves.
  server.AttachSaveStorage(storage);
  SECTION("queued") {}
  SECTION("in flight") {
    storage->defer = true;
    server.worldState.Tick();
    REQUIRE(static_cast<bool>(storage->deferred));
  }
  REQUIRE_THROWS_AS(server.worldState.AttachSaveStorage(nullptr), std::runtime_error);
  REQUIRE_THROWS_AS(server.worldState.AttachSaveStorage(
    std::make_shared<CallbackFileStorage>()), std::runtime_error);
  server.worldState.AttachSaveStorage(storage); // Idempotent outside pumping.
  if (storage->deferred) {
    auto cb = std::move(storage->deferred);
    cb();
  } else server.worldState.Tick();
  REQUIRE(storage->Read(actor).profileId == 42);
  server.worldState.AttachSaveStorage(nullptr);
}

TEST_CASE("Save pump releases its guard after exceptional callbacks", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  storage->onTick = [] { throw 17; };
  REQUIRE_THROWS_AS(server.worldState.Tick(), int);
  SetProgress(actor, "2");
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "2");
  server.worldState.AttachSaveStorage(nullptr);
}

TEST_CASE("Consumed submission failures preserve new callback snapshots", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  storage->failSubmission = true;
  MpActor* other = nullptr;
  storage->onSubmit = [&] {
    SetProgress(actor, "2");
    other = &AddHuman(server, 0xff000abd);
    SetProgress(*other, "3");
  };
  server.worldState.Tick();
  REQUIRE(storage->writes == 0);
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "2");
  REQUIRE(other);
  REQUIRE(storage->Read(*other).dynamicFields.GetValueDump("thornswoodQuests") == "3");
}

TEST_CASE("Unconsumed failures retain saves for destroyed forms", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  auto& other = AddHuman(server, 0xff000abd);
  const auto desc = actor.GetChangeForm().formDesc;
  server.worldState.Tick();
  actor.Delete();
  server.DestroyActor(0xff000abc);
  REQUIRE_FALSE(server.worldState.LookupFormById(0xff000abc));
  SetProgress(other, "1");
  storage->failBeforeConsumption = true;
  storage->onSubmit = [&] { SetProgress(other, "2"); };
  server.worldState.Tick();
  server.worldState.Tick();
  auto saved = Viet::ISaveStorageUtils::FindSync(*storage, desc);
  REQUIRE(saved);
  REQUIRE(saved->isDeleted);
  REQUIRE(storage->Read(other).dynamicFields.GetValueDump("thornswoodQuests") == "2");
}

TEST_CASE("Recursive public ticks leave packet work untouched", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  AddHuman(server);
  DoConnect(server, 0);
  server.RequestPacketHistoryPlayback(0, PacketHistory{});
  bool called = false;
  storage->onSubmit = [&] {
    called = true;
    REQUIRE_THROWS_AS(server.Tick(), std::runtime_error);
    REQUIRE(server.serverState.requestedPlaybacks.count(0) == 1);
  };
  server.worldState.Tick();
  REQUIRE(called);
  server.Tick();
  REQUIRE(server.serverState.requestedPlaybacks.empty());
}

TEST_CASE("Failed saves retain deletions across actor index reuse", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  const auto desc = actor.GetChangeForm().formDesc;
  const auto idx = actor.GetIdx();
  server.worldState.Tick();
  actor.Delete();
  server.DestroyActor(0xff000abc);
  storage->failBeforeConsumption = true;
  MpActor* replacement = nullptr;
  storage->onSubmit = [&] {
    replacement = &AddHuman(server, 0xff000abd);
    REQUIRE(replacement->GetIdx() == idx);
    SetProgress(*replacement, "2");
  };
  SECTION("standard exception") { server.worldState.Tick(); }
  SECTION("nonstandard exception") {
    storage->throwNonstandard = true;
    REQUIRE_THROWS_AS(server.worldState.Tick(), int);
  }
  // Both identities must survive: the failed batch, then newly queued work.
  server.worldState.Tick();
  server.worldState.Tick();
  auto deleted = Viet::ISaveStorageUtils::FindSync(*storage, desc);
  REQUIRE(deleted);
  REQUIRE(deleted->isDeleted);
  REQUIRE(replacement);
  REQUIRE(storage->Read(*replacement).dynamicFields.GetValueDump("thornswoodQuests") == "2");
}

TEST_CASE("Failure recovery preserves queued state over silent live changes", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  const auto queuedPosition = actor.GetPos();
  storage->failBeforeConsumption = true;
  storage->onSubmit = [&] {
    SetProgress(actor, "2");
    actor.SetPosAndAngleSilent({9, 9, 9}, {});
  };
  server.worldState.Tick();
  SECTION("single submission failure") {}
  SECTION("a second submission failure") {
    storage->failBeforeConsumption = true;
    server.worldState.Tick();
  }
  server.worldState.Tick();
  auto saved = storage->Read(actor);
  REQUIRE(saved.dynamicFields.GetValueDump("thornswoodQuests") == "2");
  REQUIRE(saved.position == queuedPosition);
  REQUIRE(actor.GetPos() == NiPoint3{9, 9, 9});
}

TEST_CASE("Newer form snapshots supersede retries across actor indices", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  const auto idx = actor.GetIdx();
  const auto desc = actor.GetChangeForm().formDesc;
  server.worldState.Tick();
  actor.Delete();
  server.DestroyActor(0xff000abc);
  storage->failBeforeConsumption = true;
  storage->onSubmit = [&] {
    auto& other = AddHuman(server, 0xff000abd);
    REQUIRE(other.GetIdx() == idx);
    auto& recreated = AddHuman(server);
    REQUIRE(recreated.GetIdx() != idx);
    SetProgress(recreated, "3");
  };
  server.worldState.Tick();
  server.worldState.Tick();
  auto saved = Viet::ISaveStorageUtils::FindSync(*storage, desc);
  REQUIRE(saved);
  REQUIRE_FALSE(saved->isDeleted);
  REQUIRE(saved->dynamicFields.GetValueDump("thornswoodQuests") == "3");
}

TEST_CASE("Failed storage batches recover by form identity", "[save-pump]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  server.worldState.Tick();
  actor.SetPosAndAngleSilent({9, 9, 9}, {});
  storage->onTick = [&] {
    std::vector<std::optional<MpChangeForm>> affected(4);
    REQUIRE(actor.GetIdx() != 3);
    affected[3] = actor.GetChangeForm();
    throw Viet::AsyncSaveStorage<MpChangeForm, FormDesc,
      std::vector<FormDesc>>::UpsertFailedException(std::move(affected),
        "injected mixed-batch failure");
  };
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).position == NiPoint3{9, 9, 9});
}

TEST_CASE("Save failure observation starts clear", "[save-failure]")
{
  WorldState empty;
  REQUIRE_FALSE(empty.HasSaveStorageFailure());
  empty.Tick();
  REQUIRE_FALSE(empty.HasSaveStorageFailure());
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  AddHuman(server);
  server.worldState.Tick();
  REQUIRE(storage->writes == 1);
  REQUIRE_FALSE(server.worldState.HasSaveStorageFailure());
}

TEST_CASE("Every observed pump failure stays latched", "[save-failure]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  AddHuman(server);
  bool escapes = false, nonstandard = false;
  bool checkBeforeResubmission = false, sawFailureBeforeSubmission = false;
  SECTION("handled generic tick error") {
    storage->onTick = [] { throw std::runtime_error("injected tick failure"); };
    checkBeforeResubmission = true;
  }
  SECTION("handled failed batch") {
    storage->onTick = [] {
      throw Viet::AsyncSaveStorage<MpChangeForm, FormDesc,
        std::vector<FormDesc>>::UpsertFailedException({}, "injected batch failure");
    };
    checkBeforeResubmission = true;
  }
  SECTION("unconsumed standard submission") {
    storage->failBeforeConsumption = true;
  }
  SECTION("consumed standard submission") {
    storage->failSubmission = true;
  }
  SECTION("nonstandard submission") {
    storage->failBeforeConsumption = true;
    storage->throwNonstandard = true;
    escapes = nonstandard = true;
  }
  SECTION("nonstandard tick") {
    storage->onTick = [] { throw 17; };
    escapes = nonstandard = true;
  }
  SECTION("standard recycle error") {
    storage->onRecycle = [] { throw std::runtime_error("injected recycle failure"); };
    escapes = true;
  }
  SECTION("nonstandard recycle error") {
    storage->onRecycle = [] { throw 17; };
    escapes = nonstandard = true;
  }
  SECTION("caught recursive public tick") {
    storage->onTick = [&] {
      REQUIRE_THROWS_AS(server.worldState.Tick(), std::runtime_error);
    };
  }
  if (checkBeforeResubmission) {
    storage->onSubmit = [&] {
      sawFailureBeforeSubmission = server.worldState.HasSaveStorageFailure();
    };
  }
  if (nonstandard) {
    try {
      server.worldState.Tick();
      FAIL("Nonstandard save failure was swallowed");
    } catch (int value) {
      REQUIRE(value == 17);
    }
  } else if (escapes) {
    REQUIRE_THROWS_AS(server.worldState.Tick(), std::runtime_error);
  } else {
    REQUIRE_NOTHROW(server.worldState.Tick());
  }
  REQUIRE(server.worldState.HasSaveStorageFailure());
  if (checkBeforeResubmission) REQUIRE(sawFailureBeforeSubmission);
  server.worldState.Tick();
  server.worldState.Tick();
  REQUIRE(storage->writes >= 1);
  REQUIRE(server.worldState.HasSaveStorageFailure());
  bool crossThreadObservation = false;
  std::thread observer([&] {
    crossThreadObservation = server.worldState.HasSaveStorageFailure();
  });
  observer.join();
  REQUIRE(crossThreadObservation);
  server.worldState.Clear();
  REQUIRE(server.worldState.HasSaveStorageFailure());
  server.worldState.AttachSaveStorage(nullptr);
  server.worldState.AttachSaveStorage(storage);
  REQUIRE(server.worldState.HasSaveStorageFailure());
  WorldState fresh;
  REQUIRE_FALSE(fresh.HasSaveStorageFailure());
}

TEST_CASE("Stale acknowledgements cannot erase an observed failure", "[save-failure]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  storage->defer = true;
  server.worldState.Tick();
  auto staleAcknowledgement = std::move(storage->deferred);
  REQUIRE(staleAcknowledgement);
  REQUIRE_FALSE(server.worldState.HasSaveStorageFailure());
  storage->onTick = [] { throw std::runtime_error("injected tick failure"); };
  server.worldState.Tick();
  REQUIRE(server.worldState.HasSaveStorageFailure());
  staleAcknowledgement();
  REQUIRE(server.worldState.HasSaveStorageFailure());
  storage->defer = false;
  SetProgress(actor, "3");
  server.worldState.Tick();
  REQUIRE(storage->Read(actor).dynamicFields.GetValueDump("thornswoodQuests") == "3");
  REQUIRE(server.worldState.HasSaveStorageFailure());
}

TEST_CASE("Pending deletions survive reused indices", "[save-identity]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  const auto index = actor.GetIdx();
  const auto description = actor.GetChangeForm().formDesc;
  auto restoredOther = actor.GetChangeForm();
  restoredOther.formDesc = FormDesc::FromFormId(0xff000abd, server.worldState.espmFiles);
  server.worldState.Tick();
  actor.Delete();
  server.DestroyActor(0xff000abc);
  SECTION("new actor initialization") {
    auto& other = AddHuman(server, 0xff000abd);
    REQUIRE(other.GetIdx() == index);
    SetProgress(other, "7");
  }
  SECTION("saved actor restoration") {
    server.worldState.LoadChangeForm(restoredOther, server.CreateFormCallbacks());
    REQUIRE(server.worldState.GetFormAt<MpActor>(0xff000abd).GetIdx() == index);
  }
  server.worldState.Tick();
  auto saved = Viet::ISaveStorageUtils::FindSync(*storage, description);
  REQUIRE(saved);
  REQUIRE(saved->isDeleted);
}

TEST_CASE("Prepared saves refuse another persistent identity", "[save-identity]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  auto& other = AddHuman(server, 0xff000abd);
  auto foreign = actor.GetChangeForm();
  foreign.formDesc = other.GetChangeForm().formDesc;
  REQUIRE_FALSE(server.worldState.RequestPreparedSave(actor, std::move(foreign)));
}

TEST_CASE("Initialization-only saves retain their original instance", "[save-identity]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto actor = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::FromFormId(0x3c, server.worldState.espmFiles)},
    server.CreateFormCallbacks(), 0x7);
  server.worldState.AddForm(std::move(actor), 0xff000abc);
  const auto desc = server.worldState.GetFormAt<MpActor>(0xff000abc).GetChangeForm().formDesc;
  storage->failSubmission = true;
  server.worldState.Tick();
  server.worldState.Tick();
  REQUIRE(Viet::ISaveStorageUtils::FindSync(*storage, desc).has_value());
}

TEST_CASE("Nested initialization retains the outer save origin", "[save-identity]")
{
  struct Actor : MpActor {
    using MpActor::MpActor;
    std::function<void(Actor&)> afterInit;
    void Init(WorldState* world, uint32_t id, bool loaded) override {
      MpActor::Init(world, id, loaded);
      if (afterInit) afterInit(*this);
    }
  };
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  const LocationalData location{{1, 1, 1}, {}, FormDesc::FromFormId(0x3c, server.worldState.espmFiles)};
  auto outer = std::make_unique<Actor>(location, server.CreateFormCallbacks(), 0x7);
  outer->afterInit = [&](Actor& initializingOuter) {
    auto inner = std::make_unique<Actor>(location, server.CreateFormCallbacks(), 0x7);
    inner->afterInit = [&](Actor&) { server.worldState.RequestSave(initializingOuter); };
    server.worldState.AddForm(std::move(inner), 0xff000abd);
  };
  server.worldState.AddForm(std::move(outer), 0xff000abc);
  const auto desc = server.worldState.GetFormAt<MpActor>(0xff000abc).GetChangeForm().formDesc;
  storage->failSubmission = true;
  server.worldState.Tick();
  server.worldState.Tick();
  REQUIRE(Viet::ISaveStorageUtils::FindSync(*storage, desc).has_value());
}

TEST_CASE("Retained snapshots cannot acknowledge a replacement instance", "[save-identity]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  auto& actor = AddHuman(server);
  server.worldState.Tick();
  const auto inventory = actor.GetInventory();
  REQUIRE(actor.CompareAndSetInventory(inventory, "null", inventory, 1));
  const auto originalReceipt = actor.GetInventoryReceiptDump();
  auto replacementState = actor.GetChangeForm();
  auto replacementReceipt = nlohmann::json::parse(originalReceipt);
  replacementReceipt["sequence"] = 2;
  const auto replacementDump = replacementReceipt.dump();
  replacementState.dynamicFields.SetValueDump(
    MpObjectReference::kInventoryReceiptProperty, replacementDump);
  auto replace = [&] {
    server.DestroyActor(0xff000abc);
    server.worldState.LoadChangeForm(replacementState, server.CreateFormCallbacks());
    auto& replacement = server.worldState.GetFormAt<MpActor>(0xff000abc);
    REQUIRE(server.worldState.GetSavedInventoryReceipt(replacement) == replacementDump);
  };
  SECTION("pending row") { replace(); }
  SECTION("unconsumed retry row") {
    storage->failBeforeConsumption = true;
    storage->onSubmit = replace;
    server.worldState.Tick();
  }
  server.worldState.Tick();
  auto& replacement = server.worldState.GetFormAt<MpActor>(0xff000abc);
  REQUIRE(storage->Read(replacement).dynamicFields.GetValueDump(
    MpObjectReference::kInventoryReceiptProperty) == originalReceipt);
  REQUIRE(server.worldState.GetSavedInventoryReceipt(replacement) == replacementDump);
}

TEST_CASE("Deferred initialization cannot replace an accepted pending row", "[save-identity]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  server.worldState.npcEnabled = true;
  server.worldState.npcAllowEssential = true;
  server.worldState.npcAllowCrimeFaction = true;
  auto storage = std::make_shared<CallbackFileStorage>();
  server.AttachSaveStorage(storage);
  constexpr uint32_t cowId = 0x10ebaf;
  auto& cow = server.worldState.GetFormAt<MpActor>(cowId);
  SetProgress(cow, "1");
  server.worldState.Tick();
  auto saved = cow.GetChangeForm();
  SetProgress(cow, "2");
  server.worldState.DestroyForm<MpActor>(cowId);
  server.worldState.LoadChangeForm(saved, server.CreateFormCallbacks());
  REQUIRE_FALSE(server.worldState.LookupFormByIdNoLoad(cowId));
  auto& restored = server.worldState.GetFormAt<MpActor>(cowId);
  server.worldState.Tick();
  REQUIRE(storage->Read(restored).dynamicFields.GetValueDump("thornswoodQuests") == "2");
}

namespace {
struct CopySensitiveSnapshot
{
  int formDesc;
  int value;
  std::shared_ptr<int> copiesRemaining;
  CopySensitiveSnapshot(int identity, int value, std::shared_ptr<int> budget)
    : formDesc(identity), value(value), copiesRemaining(std::move(budget)) {}
  CopySensitiveSnapshot(const CopySensitiveSnapshot& other)
    : formDesc(other.formDesc), value(other.value), copiesRemaining(other.copiesRemaining)
  {
    if (*copiesRemaining == 0) throw std::runtime_error("injected preparation copy failure");
    --*copiesRemaining;
  }
  CopySensitiveSnapshot(CopySensitiveSnapshot&&) noexcept = default;
  CopySensitiveSnapshot& operator=(const CopySensitiveSnapshot&) = default;
  CopySensitiveSnapshot& operator=(CopySensitiveSnapshot&&) noexcept = default;
};
}

TEST_CASE("Failed batch preparation preserves all original rows and origins", "[save-identity]")
{
  using Entry = PendingSaveDetail::Entry<CopySensitiveSnapshot, int>;
  using Batch = PendingSaveDetail::Batch<CopySensitiveSnapshot, int, int>;
  auto budget = std::make_shared<int>(1);
  std::map<int, Entry> pending;
  pending.emplace(1, Entry{CopySensitiveSnapshot{1, 10, budget}, 101});
  pending.emplace(2, Entry{CopySensitiveSnapshot{2, 20, budget}, 202});
  Batch retry;
  retry.rows.emplace_back(CopySensitiveSnapshot{3, 30, budget});
  retry.origins.emplace(3, 303);
  REQUIRE_THROWS_AS(PendingSaveDetail::Prepare(pending, retry), std::runtime_error);
  REQUIRE(pending.size() == 2);
  REQUIRE(pending.at(1).snapshot.value == 10);
  REQUIRE(pending.at(2).snapshot.value == 20);
  REQUIRE(pending.at(1).origin == 101);
  REQUIRE(pending.at(2).origin == 202);
  REQUIRE(retry.rows.size() == 1);
  REQUIRE(retry.rows[0]->value == 30);
  REQUIRE(retry.origins.at(3) == 303);
  *budget = 20;
  pending.emplace(3, Entry{CopySensitiveSnapshot{3, 99, budget}, 999});
  auto candidate = PendingSaveDetail::Prepare(pending, retry);
  REQUIRE(candidate.rows.size() == 3);
  REQUIRE(candidate.rows[2]->value == 99);
  REQUIRE(candidate.origins.at(3) == 999);
  REQUIRE(retry.rows[0]->value == 30);
  pending.erase(3);
  retry.origins.erase(3);
  REQUIRE_THROWS_AS(PendingSaveDetail::Prepare(pending, retry), std::out_of_range);
  REQUIRE(retry.rows[0]->value == 30);
  REQUIRE(pending.size() == 2);
}
