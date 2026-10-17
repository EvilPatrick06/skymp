#include "TestUtils.hpp"
#include "WorldState.h"
#include "save_storages/ISaveStorage.h"
#include <functional>
#include <thread>

extern espm::Loader& GetEspmLoader();

namespace {
class CompletionStorage
  : public Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  std::map<FormDesc, MpChangeForm> saved;
  std::vector<std::optional<MpChangeForm>> pending;
  UpsertOutcomeCallback outcome;
  std::function<void()> onTick, onSubmit, onRecycle;
  mutable std::function<void()> onSettlement;
  bool deferred = false, ready = false, failOutcome = false;
  bool settled = true;
  uint32_t submissions = 0, writes = 0;

  static void Once(std::function<void()>& hook)
  {
    auto callback = std::move(hook);
    hook = {};
    if (callback) callback();
  }
  void Commit(std::vector<std::optional<MpChangeForm>>&& rows)
  {
    for (auto& row : rows) if (row) saved.insert_or_assign(row->formDesc, std::move(*row));
    ++writes;
  }
  bool SupportsUpsertOutcomes() const noexcept override { return true; }
  void UpsertWithOutcome(std::vector<std::optional<MpChangeForm>>&& rows,
                         const UpsertOutcomeCallback& callback) override
  {
    ++submissions;
    Once(onSubmit);
    if (deferred) {
      pending = std::move(rows);
      outcome = callback;
    } else {
      Commit(std::move(rows));
      callback(nullptr);
    }
  }
  void Upsert(std::vector<std::optional<MpChangeForm>>&& rows,
              const UpsertCallback& callback) override
  {
    UpsertWithOutcome(std::move(rows), [callback](std::exception_ptr error) {
      if (error) std::rethrow_exception(error);
      callback();
    });
  }
  void Tick() override
  {
    Once(onTick);
    if (!outcome || !ready) return;
    ready = false;
    auto callback = std::move(outcome);
    if (failOutcome) {
      pending.clear();
      callback(std::make_exception_ptr(std::runtime_error("controlled failed save")));
    } else {
      Commit(std::move(pending));
      callback(nullptr);
    }
  }
  bool IsSettled() const override
  {
    Once(onSettlement);
    return settled && !outcome;
  }
  void IterateSync(const IterateSyncCallback& callback) override
  { for (const auto& row : saved) callback(row.second); }
  void Iterate(const IterateCallback& callback,
               const std::optional<std::vector<FormDesc>>&) override
  {
    std::vector<MpChangeForm> rows;
    for (const auto& row : saved) rows.push_back(row.second);
    callback(rows);
  }
  bool GetRecycledChangeFormsBuffer(std::vector<std::optional<MpChangeForm>>&) override
  { Once(onRecycle); return false; }
  uint32_t GetNumFinishedUpserts() const override { return writes; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  const std::string& GetName() const override
  { static const std::string name = "controlled-completion"; return name; }
};

struct CompletionWorld
{
  PartOne server;
  std::shared_ptr<CompletionStorage> storage = std::make_shared<CompletionStorage>();
  CompletionWorld()
  {
    server.AttachEspm(&GetEspmLoader());
    server.AttachSaveStorage(storage);
    server.CreateActor(0xff000abc, {1, 1, 1}, 0, 0x3c, 42);
  }
  MpActor& Actor() { return server.worldState.GetFormAt<MpActor>(0xff000abc); }
  void Progress(const char* value)
  { Actor().SetPropertyValueDump("thornswoodQuests", value, false, false); }
  const MpChangeForm& Saved() { return storage->saved.at(Actor().GetChangeForm().formDesc); }
};
}

TEST_CASE("Terminal completion drains accepted metadata without inventing live state", "[save-completion]")
{
  CompletionWorld fixture;
  fixture.Progress("1");
  const auto receipt = fixture.Actor().GetInventoryReceiptDump();
  fixture.Actor().SetPosAndAngleSilent({9, 9, 9}, {});
  bool timerFired = false;
  fixture.server.worldState.SetTimer(std::chrono::milliseconds(0)).Then(
    [&](Viet::Void) { timerFired = true; });
  const auto result = fixture.server.worldState.CompleteSavesForShutdown(1000);
  REQUIRE(result.phase == WorldState::SaveCompletionResult::Phase::WritesCompleted);
  REQUIRE_FALSE(result.supportsDurableUpsert);
  REQUIRE(fixture.Saved().dynamicFields.GetValueDump("thornswoodQuests") == "1");
  REQUIRE(fixture.Saved().position == NiPoint3{1, 1, 1});
  REQUIRE(fixture.Actor().GetInventoryReceiptDump() == receipt);
  REQUIRE_FALSE(timerFired);
  REQUIRE_THROWS(fixture.server.worldState.Tick());
  REQUIRE_THROWS(fixture.server.worldState.AttachSaveStorage(fixture.storage));
  REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(1000));
}

TEST_CASE("Terminal completion waits for an existing outcome and newer accepted work", "[save-completion]")
{
  CompletionWorld fixture;
  fixture.storage->deferred = true;
  fixture.Progress("1");
  fixture.server.worldState.Tick();
  REQUIRE(fixture.storage->outcome);
  fixture.Progress("2");
  fixture.storage->ready = true;
  fixture.storage->deferred = false;
  fixture.server.worldState.CompleteSavesForShutdown(1000);
  REQUIRE(fixture.Saved().dynamicFields.GetValueDump("thornswoodQuests") == "2");
  REQUIRE(fixture.storage->writes == 2);
}

TEST_CASE("Terminal completion includes work queued at storage boundaries", "[save-completion]")
{
  CompletionWorld fixture;
  fixture.Progress("1");
  auto more = [&] { fixture.Progress("2"); };
  SECTION("submission") { fixture.storage->onSubmit = more; }
  SECTION("settlement observation") { fixture.storage->onSettlement = more; }
  SECTION("recycling") { fixture.storage->onRecycle = more; }
  fixture.server.worldState.CompleteSavesForShutdown(1000);
  REQUIRE(fixture.Saved().dynamicFields.GetValueDump("thornswoodQuests") == "2");
  REQUIRE(fixture.storage->writes == 2);
}

TEST_CASE("Terminal completion preserves an accepted deletion over a restored view", "[save-completion]")
{
  CompletionWorld fixture;
  auto restored = fixture.Actor().GetChangeForm();
  const auto desc = restored.formDesc;
  fixture.Actor().Delete();
  fixture.server.DestroyActor(0xff000abc);
  fixture.server.worldState.LoadChangeForm(restored, fixture.server.CreateFormCallbacks());
  fixture.server.worldState.CompleteSavesForShutdown(1000);
  REQUIRE(fixture.storage->saved.at(desc).isDeleted);
}

TEST_CASE("Terminal save errors prevent any following submission", "[save-completion]")
{
  CompletionWorld fixture;
  uint32_t expectedSubmissions = 0;
  SECTION("uncorrelated tick error") {
    fixture.storage->onTick = [] { throw std::runtime_error("tick error"); };
  }
  SECTION("nonstandard tick error") { fixture.storage->onTick = [] { throw 17; }; }
  SECTION("submission error") {
    expectedSubmissions = 1;
    fixture.storage->onSubmit = [] { throw std::runtime_error("submission error"); };
  }
  SECTION("nonstandard submission error") {
    expectedSubmissions = 1;
    fixture.storage->onSubmit = [] { throw 17; };
  }
  SECTION("recycling error") {
    expectedSubmissions = 1;
    fixture.storage->onRecycle = [] { throw std::runtime_error("recycling error"); };
  }
  SECTION("settlement error") {
    expectedSubmissions = 1;
    fixture.storage->onSettlement = [] { throw std::runtime_error("settlement error"); };
  }
  SECTION("failed existing outcome") {
    expectedSubmissions = 1;
    fixture.storage->deferred = true;
    fixture.server.worldState.Tick();
    fixture.Progress("2");
    fixture.storage->ready = fixture.storage->failOutcome = true;
  }
  REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(1000));
  REQUIRE(fixture.storage->submissions == expectedSubmissions);
  REQUIRE(fixture.server.worldState.HasSaveStorageFailure());
  REQUIRE_THROWS(fixture.server.worldState.Tick());
  REQUIRE_THROWS(fixture.server.worldState.AttachSaveStorage(nullptr));
}

TEST_CASE("Terminal save deadlines cannot certify late work", "[save-completion]")
{
  CompletionWorld fixture;
  uint32_t expectedSubmissions = 0;
  auto slow = [] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); };
  SECTION("before submission") { fixture.storage->onTick = slow; }
  SECTION("after submission") {
    expectedSubmissions = 1;
    fixture.storage->onSubmit = slow;
  }
  SECTION("settlement observation") {
    expectedSubmissions = 1;
    fixture.storage->onSettlement = slow;
  }
  REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(100));
  REQUIRE(fixture.storage->submissions == expectedSubmissions);
  REQUIRE_THROWS(fixture.server.worldState.Tick());
}

TEST_CASE("Caught recursive calls cannot produce a terminal success", "[save-completion]")
{
  CompletionWorld fixture;
  fixture.storage->onTick = [&] {
    SECTION("recursive completion") {
      REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(1000));
    }
    SECTION("gameplay tick") { REQUIRE_THROWS(fixture.server.Tick()); }
    SECTION("idempotent attachment") {
      REQUIRE_THROWS(fixture.server.worldState.AttachSaveStorage(fixture.storage));
    }
  };
  REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(1000));
  REQUIRE(fixture.storage->submissions == 0);
  REQUIRE_THROWS(fixture.server.worldState.Tick());
}

TEST_CASE("Terminal completion rejects missing prerequisites", "[save-completion]")
{
  SECTION("no storage") {
    WorldState world;
    REQUIRE_THROWS(world.CompleteSavesForShutdown(1000));
  }
  SECTION("settlement never supported") {
    CompletionWorld fixture;
    fixture.storage->settled = false;
    REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(2));
    REQUIRE_THROWS(fixture.server.worldState.Tick());
  }
  SECTION("previously observed failure") {
    CompletionWorld fixture;
    fixture.storage->onTick = [] { throw std::runtime_error("earlier error"); };
    fixture.server.worldState.Tick();
    const auto submissions = fixture.storage->submissions;
    REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(1000));
    REQUIRE(fixture.storage->submissions == submissions);
  }
}

TEST_CASE("Invalid terminal requests leave ordinary saving available", "[save-completion]")
{
  CompletionWorld fixture;
  SECTION("zero timeout") { REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(0)); }
  SECTION("excessive timeout") { REQUIRE_THROWS(fixture.server.worldState.CompleteSavesForShutdown(30001)); }
  SECTION("another thread") {
    bool refused = false;
    std::thread other([&] {
      try { fixture.server.worldState.CompleteSavesForShutdown(1000); }
      catch (...) { refused = true; }
    });
    other.join();
    REQUIRE(refused);
  }
  fixture.server.worldState.Tick();
  REQUIRE(fixture.storage->writes == 1);
}

TEST_CASE("Terminal completion refuses active deferred-form loading", "[save-completion]")
{
  CompletionWorld fixture;
  auto& world = fixture.server.worldState;
  world.npcEnabled = true;
  world.npcAllowEssential = true;
  world.npcAllowCrimeFaction = true;
  constexpr uint32_t cowId = 0x10ebaf;
  auto saved = world.GetFormAt<MpActor>(cowId).GetChangeForm();
  world.DestroyForm<MpActor>(cowId);
  world.LoadChangeForm(saved, fixture.server.CreateFormCallbacks());
  bool checked = false;
  world.AttachEspm(&GetEspmLoader(), [&] {
    checked = true;
    REQUIRE_THROWS(world.CompleteSavesForShutdown(1000));
    return fixture.server.CreateFormCallbacks();
  });
  world.GetFormAt<MpActor>(cowId);
  REQUIRE(checked);
  world.Tick();
  REQUIRE(fixture.storage->writes == 1);
}
