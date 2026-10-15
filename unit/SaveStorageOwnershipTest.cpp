#include "TestUtils.hpp"
#include "WorldState.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/ISaveStorage.h"
#include "save_storages/AsyncSaveStorage.h"
#include <chrono>
#include <filesystem>
#include <functional>

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
