#include "save_storages/AsyncSaveStorage.h"
#include <condition_variable>
#include <iostream>

namespace Viet {
struct AsyncSaveStorageTestPeer
{
  // Establish worker quiescence without changing failure or callback state.
  template <class T, class FormDesc, class Filter>
  static void StopWorker(AsyncSaveStorage<T, FormDesc, Filter>& storage)
  {
    storage.pImpl->destroyed = true;
    if (storage.pImpl->thr->joinable()) {
      storage.pImpl->thr->join();
    }
  }
  template <class T, class FormDesc, class Filter>
  static bool HasActivePublicOperation(
    const AsyncSaveStorage<T, FormDesc, Filter>& storage)
  {
    return storage.pImpl->publicOperations != 0;
  }
};
}

namespace {
using Database = Viet::IDatabase<int, int, std::vector<int>>;
using Storage = Viet::ISaveStorage<int, int, std::vector<int>>;
using Async = Viet::AsyncSaveStorage<int, int, std::vector<int>>;

unsigned checks = 0;
void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Gate
{
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool released = false;
  void Enter()
  {
    std::unique_lock lock(mutex);
    entered = true;
    condition.notify_all();
    condition.wait(lock, [&] { return released; });
  }
  void Wait()
  {
    std::unique_lock lock(mutex);
    Check(condition.wait_for(lock, std::chrono::seconds(3),
                             [&] { return entered; }),
          "Worker did not reach the deterministic boundary");
  }
  void Release()
  {
    std::lock_guard lock(mutex);
    released = true;
    condition.notify_all();
  }
};

struct ReleaseOnExit
{
  std::shared_ptr<Gate> gate;
  ~ReleaseOnExit() { gate->Release(); }
};

class TestDatabase : public Database
{
public:
  std::shared_ptr<Gate> upsertGate;
  std::shared_ptr<Gate> iterateGate;
  bool failUpsert = false;
  bool failIterate = false;
  bool swallowCallback = false;
  void Iterate(const IterateCallback& callback,
               std::optional<std::vector<int>>) override
  {
    if (iterateGate) {
      iterateGate->Enter();
    }
    if (failIterate) {
      throw std::runtime_error("Injected database failure");
    }
    try {
      callback(7);
    } catch (...) {
      if (!swallowCallback) {
        throw;
      }
    }
  }

protected:
  std::vector<std::optional<int>>&& UpsertImpl(
    std::vector<std::optional<int>>&& values, size_t& inserted) override
  {
    if (upsertGate) {
      upsertGate->Enter();
    }
    if (failUpsert) {
      throw std::runtime_error("Injected database failure");
    }
    inserted = values.size();
    return std::move(values);
  }
};

class LegacyStorage : public Storage
{
public:
  void IterateSync(const IterateSyncCallback&) override {}
  void Upsert(std::vector<std::optional<int>>&&,
              const UpsertCallback&) override {}
  void Iterate(const IterateCallback&,
               const std::optional<std::vector<int>>&) override {}
  uint32_t GetNumFinishedUpserts() const override { return 0; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  void Tick() override {}
  bool GetRecycledChangeFormsBuffer(
    std::vector<std::optional<int>>&) override { return false; }
  const std::string& GetName() const override
  {
    static const std::string name = "legacy";
    return name;
  }
};

template <class SaveStorage, class Predicate>
void PumpUntil(SaveStorage& storage, Predicate done)
{
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds(3);
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("Storage completion deadline expired");
    }
    storage.Tick();
    std::this_thread::yield();
  }
}

void DelayedIterate()
{
  auto database = std::make_shared<TestDatabase>();
  database->upsertGate = std::make_shared<Gate>();
  database->iterateGate = std::make_shared<Gate>();
  Async storage(database, nullptr, "", 1);
  ReleaseOnExit releaseUpsert{ database->upsertGate };
  ReleaseOnExit release{ database->iterateGate };
  bool firstAck = false;
  bool iterated = false;
  bool nextAck = false;
  storage.Upsert({ 1 }, [&] { firstAck = true; });
  database->upsertGate->Wait();
  storage.Iterate([&](const std::vector<int>& rows) {
    Check(rows == std::vector<int>{ 7 }, "Wrong iterate values");
    iterated = true;
    storage.Upsert({ 2 }, [&] { nextAck = true; });
  }, std::nullopt);
  database->upsertGate->Release();
  database->iterateGate->Wait();
  storage.Tick();
  Check(firstAck, "Earlier upsert acknowledgement was not published");
  Check(!storage.IsSettled(), "Detached iterate looked settled");
  Check(!iterated, "Blocked iterate callback ran early");
  database->iterateGate->Release();
  PumpUntil(storage, [&] { return iterated; });
  Check(!storage.IsSettled(), "Callback-enqueued save looked settled");
  PumpUntil(storage, [&] { return nextAck && storage.IsSettled(); });
  Check(nextAck, "Callback-enqueued save was lost");
}

void BlockedUpsert()
{
  auto database = std::make_shared<TestDatabase>();
  database->upsertGate = std::make_shared<Gate>();
  Async storage(database, nullptr, "", 1);
  ReleaseOnExit release{ database->upsertGate };
  bool acknowledged = false;
  storage.Upsert({ 1 }, [&] { acknowledged = true; });
  database->upsertGate->Wait();
  Check(!storage.IsSettled(), "Active upsert looked settled");
  database->upsertGate->Release();
  PumpUntil(storage, [&] { return acknowledged; });
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Prepared callback captures remain alive");
  PumpUntil(storage, [&] { return storage.IsSettled(); });
}

void WorkerFailure(bool iterate)
{
  auto database = std::make_shared<TestDatabase>();
  database->failIterate = iterate;
  database->failUpsert = !iterate;
  Async storage(database, nullptr, "", 1);
  bool acknowledged = false;
  if (iterate) {
    storage.Iterate([&](const auto&) { acknowledged = true; }, std::nullopt);
  } else {
    storage.Upsert({ 1 }, [&] { acknowledged = true; });
  }
  bool failed = false;
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds(3);
  while (!failed && std::chrono::steady_clock::now() < deadline) {
    try {
      storage.Tick();
    } catch (const std::runtime_error&) {
      failed = true;
    }
    std::this_thread::yield();
  }
  Check(failed, "Worker failure was not delivered");
  Check(!acknowledged, "Failed operation was acknowledged");
  storage.Tick();
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Consumed worker failure was forgotten");
}

void SynchronousFailure(bool databaseFailure, bool swallowed)
{
  auto database = std::make_shared<TestDatabase>();
  database->failIterate = databaseFailure;
  database->swallowCallback = swallowed;
  Async storage(database, nullptr, "", 1);
  bool failed = false;
  try {
    storage.IterateSync([](int) {
      throw std::runtime_error("Injected synchronous callback failure");
    });
  } catch (const std::runtime_error&) {
    failed = true;
  }
  Check(failed, "Synchronous failure was swallowed");
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Synchronous failure was forgotten");
}

struct CopyState
{
  const std::thread::id owner = std::this_thread::get_id();
  Async* storage = nullptr;
  bool probe = false;
  bool failCopy = false;
  unsigned probed = 0;
};

struct CopyObserver
{
  std::shared_ptr<CopyState> state;
  explicit CopyObserver(std::shared_ptr<CopyState> value)
    : state(std::move(value)) {}
  CopyObserver(const CopyObserver& other) : state(other.state)
  {
    if (std::this_thread::get_id() != state->owner) {
      return;
    }
    if (state->failCopy) {
      throw std::runtime_error("Injected callback copy failure");
    }
    if (state->probe) {
      Check(Viet::AsyncSaveStorageTestPeer::HasActivePublicOperation(
              *state->storage),
            "Enqueue query would try its own queue mutex");
      Check(!state->storage->IsSettled(),
            "Reentrant query during enqueue looked settled");
      ++state->probed;
    }
  }
  template <class... Args> void operator()(Args&&...) const {}
};

void SubmissionCopy(bool iterate, bool fail)
{
  Async storage(std::make_shared<TestDatabase>(), nullptr, "", 1);
  auto state = std::make_shared<CopyState>();
  state->storage = &storage;
  Async::UpsertCallback upsert = CopyObserver(state);
  Async::IterateCallback callback = CopyObserver(state);
  state->probe = !fail;
  state->failCopy = fail;
  bool failed = false;
  try {
    if (iterate) {
      storage.Iterate(callback, std::nullopt);
    } else {
      storage.Upsert({ 1 }, upsert);
    }
  } catch (const std::runtime_error&) {
    failed = true;
  }
  state->probe = false;
  state->failCopy = false;
  Check(failed == fail, "Wrong submission copy result");
  if (fail) {
    Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
    Check(!storage.IsSettled(), "Submission failure was forgotten");
  } else {
    Check(state->probed > 0, "No enqueue copy was observed");
    PumpUntil(storage, [&] { return storage.IsSettled(); });
  }
}

void TickFailure(bool recursive)
{
  Async storage(std::make_shared<TestDatabase>(), nullptr, "", 1);
  bool failed = false;
  if (recursive) {
    storage.Upsert({ 1 }, [&] {
      Check(!storage.IsSettled(), "Executing callback looked settled");
      try { storage.Tick(); }
      catch (const std::runtime_error&) { failed = true; }
    });
    PumpUntil(storage, [&] { return failed; });
  } else {
    std::thread tick([&] {
      try { storage.Tick(); }
      catch (const std::runtime_error&) { failed = true; }
    });
    tick.join();
  }
  Check(failed, "Unsafe tick entry was accepted");
  storage.Tick();
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Tick refusal did not retain failure");
}

void CallbackFailure()
{
  Async storage(std::make_shared<TestDatabase>(), nullptr, "", 1);
  storage.Upsert({ 1 }, [] {
    throw std::runtime_error("Injected asynchronous callback failure");
  });
  bool failed = false;
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds(3);
  while (!failed && std::chrono::steady_clock::now() < deadline) {
    try { storage.Tick(); }
    catch (const std::runtime_error&) { failed = true; }
    std::this_thread::yield();
  }
  Check(failed, "Callback failure was not delivered");
  storage.Tick();
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Callback failure was forgotten");
}

struct CaptureState
{
  const std::thread::id owner = std::this_thread::get_id();
  std::atomic<unsigned> nextId = 0;
  std::atomic<unsigned> taskId = 0;
  bool recordTaskCopy = false;
  unsigned calls = 0;
  std::shared_ptr<Gate> destruction = std::make_shared<Gate>();
};

struct WorkerCapture
{
  std::shared_ptr<CaptureState> state;
  unsigned id;
  explicit WorkerCapture(std::shared_ptr<CaptureState> value)
    : state(std::move(value)), id(++state->nextId) {}
  WorkerCapture(const WorkerCapture& other)
    : state(other.state), id(++state->nextId)
  {
    if (std::this_thread::get_id() == state->owner && state->recordTaskCopy) {
      state->taskId = id;
    }
  }
  ~WorkerCapture()
  {
    if (std::this_thread::get_id() != state->owner && id == state->taskId) {
      state->destruction->Enter();
    }
  }
  void operator()(const std::vector<int>&) const { ++state->calls; }
};

void WorkerCaptureDestruction()
{
  auto state = std::make_shared<CaptureState>();
  Async storage(std::make_shared<TestDatabase>(), nullptr, "", 1);
  ReleaseOnExit release{ state->destruction };
  Async::IterateCallback callback = WorkerCapture(state);
  state->recordTaskCopy = true;
  storage.Iterate(callback, std::nullopt);
  state->recordTaskCopy = false;
  state->destruction->Wait();
  storage.Tick();
  Check(state->calls == 1, "Callback was not published before destruction");
  storage.Tick();
  Check(!storage.IsSettled(), "Detached capture destruction looked settled");
  state->destruction->Release();
  PumpUntil(storage, [&] { return storage.IsSettled(); });
}

void FinalFailureObservation()
{
  bool failed = false;
  const bool settled = Viet::SaveStorageDetail::NoActiveOrFailedOperations(
    [] { return uint64_t(0); }, [&] {
      // The worker finishes and latches an error after initial observations.
      failed = true;
      return false;
    }, [&] { return failed; });
  Check(!settled, "Activity transition hid the final failure state");
}

struct ValueState
{
  const std::thread::id owner = std::this_thread::get_id();
  std::atomic<bool> failCopy = false;
  bool probeDestruction = false;
  std::function<void()> onDestroy;
};

struct ObservedValue
{
  std::shared_ptr<ValueState> state;
  explicit ObservedValue(std::shared_ptr<ValueState> value)
    : state(std::move(value)) {}
  ObservedValue(const ObservedValue& other) : state(other.state)
  {
    if (state->failCopy) {
      throw std::runtime_error("Injected row collection failure");
    }
  }
  ObservedValue(ObservedValue&&) noexcept = default;
  ~ObservedValue()
  {
    if (state && std::this_thread::get_id() == state->owner &&
        state->probeDestruction && state->onDestroy) {
      state->onDestroy();
    }
  }
};

class ValueDatabase
  : public Viet::IDatabase<ObservedValue, int, std::vector<int>>
{
public:
  explicit ValueDatabase(std::shared_ptr<ValueState> state)
    : value(std::move(state)) {}
  ObservedValue value;
  void Iterate(const IterateCallback& callback,
               std::optional<std::vector<int>>) override
  {
    // FileDatabase also catches row callback errors internally. The async
    // collector must retain and rethrow its own failure after this returns.
    try { callback(value); }
    catch (const std::runtime_error&) {}
  }
protected:
  std::vector<std::optional<ObservedValue>>&& UpsertImpl(
    std::vector<std::optional<ObservedValue>>&& values,
    size_t& inserted) override
  {
    inserted = values.size();
    return std::move(values);
  }
};

using ValueAsync =
  Viet::AsyncSaveStorage<ObservedValue, int, std::vector<int>>;

void SwallowedCollectorFailure()
{
  auto state = std::make_shared<ValueState>();
  auto database = std::make_shared<ValueDatabase>(state);
  ValueAsync storage(database, nullptr, "", 1);
  state->failCopy = true;
  bool called = false;
  storage.Iterate([&](const auto&) { called = true; }, std::nullopt);
  bool failed = false;
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds(3);
  while (!failed && std::chrono::steady_clock::now() < deadline) {
    try { storage.Tick(); }
    catch (const std::runtime_error&) { failed = true; }
    std::this_thread::yield();
  }
  Check(failed, "Swallowed collector failure was not propagated");
  Check(!called, "Partial iteration was published as success");
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(!storage.IsSettled(), "Collector failure was forgotten");
}

void RecycleQuery()
{
  auto state = std::make_shared<ValueState>();
  ValueAsync storage(std::make_shared<ValueDatabase>(state), nullptr, "", 1);
  bool acknowledged = false;
  storage.Upsert({ ObservedValue(state) }, [&] { acknowledged = true; });
  PumpUntil(storage, [&] { return acknowledged; });
  PumpUntil(storage, [&] { return storage.IsSettled(); });
  Viet::AsyncSaveStorageTestPeer::StopWorker(storage);
  Check(storage.IsSettled(), "Recycle test did not start from settlement");
  std::vector<std::optional<ObservedValue>> output{ ObservedValue(state) };
  bool observed = false;
  bool unguarded = false;
  bool falselySettled = false;
  state->onDestroy = [&] {
    observed = true;
    if (!Viet::AsyncSaveStorageTestPeer::HasActivePublicOperation(storage)) {
      unguarded = true;
      return; // Do not invoke undefined same-mutex try_lock in a broken build.
    }
    falselySettled = storage.IsSettled();
  };
  state->probeDestruction = true;
  const bool recycled = storage.GetRecycledChangeFormsBuffer(output);
  state->probeDestruction = false;
  state->onDestroy = {};
  Check(recycled, "Expected recycled value buffer");
  Check(observed, "Recycle did not destroy the caller's previous value");
  Check(!unguarded, "Recycle query would try its own queue mutex");
  Check(!falselySettled, "Recycle reentrancy looked settled");
  PumpUntil(storage, [&] { return storage.IsSettled(); });
}
}

int main()
{
  try {
    LegacyStorage legacy;
    Check(!legacy.IsSettled(), "Legacy storage must refuse certification");
    {
      Async storage(std::make_shared<TestDatabase>(), nullptr, "", 1);
      PumpUntil(storage, [&] { return storage.IsSettled(); });
      Check(!storage.SupportsDurableUpsert(), "Idle is not durability");
      bool otherThreadSettled = true;
      std::thread query([&] { otherThreadSettled = storage.IsSettled(); });
      query.join();
      Check(!otherThreadSettled, "Off-owner query must refuse");
    }
    DelayedIterate();
    BlockedUpsert();
    WorkerFailure(false);
    WorkerFailure(true);
    SynchronousFailure(true, false);
    SynchronousFailure(false, false);
    SynchronousFailure(false, true);
    SubmissionCopy(false, false);
    SubmissionCopy(true, false);
    SubmissionCopy(false, true);
    SubmissionCopy(true, true);
    TickFailure(false);
    TickFailure(true);
    CallbackFailure();
    WorkerCaptureDestruction();
    FinalFailureObservation();
    SwallowedCollectorFailure();
    RecycleQuery();
    std::cout << checks << " storage settlement checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
