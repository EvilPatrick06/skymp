#include "save_storages/AsyncSaveStorage.h"
#include <iostream>

namespace {
using Storage = Viet::ISaveStorage<int, int, std::vector<int>>;
using Database = Viet::IDatabase<int, int, std::vector<int>>;
using Async = Viet::AsyncSaveStorage<int, int, std::vector<int>>;
unsigned checks = 0;

void Check(bool value, const char* message)
{
  ++checks;
  if (!value) throw std::runtime_error(message);
}

class LegacyStorage : public Storage
{
public:
  void IterateSync(const IterateSyncCallback&) override {}
  void Upsert(std::vector<std::optional<int>>&&, const UpsertCallback&) override {}
  void Iterate(const IterateCallback&, const std::optional<std::vector<int>>&) override {}
  uint32_t GetNumFinishedUpserts() const override { return 0; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  void Tick() override {}
  bool GetRecycledChangeFormsBuffer(std::vector<std::optional<int>>&) override { return false; }
  const std::string& GetName() const override { static const std::string name = "legacy"; return name; }
};

class TestDatabase : public Database
{
public:
  std::atomic<unsigned> calls = 0;
  void Iterate(const IterateCallback&, std::optional<std::vector<int>>) override {}
protected:
  std::vector<std::optional<int>>&& UpsertImpl(
    std::vector<std::optional<int>>&& rows, size_t& count) override
  {
    ++calls;
    if (rows.at(0) == -1) throw std::runtime_error("first job failed");
    if (rows.at(0) == -17) throw 17;
    count = rows.size();
    return std::move(rows);
  }
};

template <class Predicate>
void PumpUntil(Async& storage, Predicate done)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Outcome deadline expired");
    storage.Tick();
    std::this_thread::yield();
  }
}

void Defaults()
{
  LegacyStorage storage;
  Check(!storage.SupportsUpsertOutcomes(), "Legacy storage opted into outcomes");
  bool refused = false, called = false;
  try { storage.UpsertWithOutcome({7}, [&](std::exception_ptr) { called = true; }); }
  catch (const std::runtime_error&) { refused = true; }
  Check(refused && !called, "Unsupported outcome submission did not refuse");
}

void DistinctOutcomes()
{
  auto database = std::make_shared<TestDatabase>();
  Async storage(database, nullptr, "", 1);
  Check(storage.SupportsUpsertOutcomes(), "Async storage did not support outcomes");
  Check(!storage.SupportsDurableUpsert(), "Outcome support certified durability");
  const auto owner = std::this_thread::get_id();
  unsigned failed = 0, succeeded = 0, nonstandard = 0;
  storage.UpsertWithOutcome({-1}, [&](std::exception_ptr error) {
    Check(std::this_thread::get_id() == owner, "Failure callback ran off owner thread");
    Check(bool(error), "Failed job reported success");
    try { std::rethrow_exception(error); }
    catch (const std::runtime_error& value) { Check(std::string(value.what()) == "first job failed", "Wrong job error"); }
    ++failed;
  });
  storage.UpsertWithOutcome({7}, [&](std::exception_ptr error) {
    Check(std::this_thread::get_id() == owner, "Success callback ran off owner thread");
    Check(!error, "Successful job inherited another job's error");
    ++succeeded;
  });
  storage.UpsertWithOutcome({-17}, [&](std::exception_ptr error) {
    Check(bool(error), "Nonstandard failure reported success");
    try { std::rethrow_exception(error); }
    catch (int value) { Check(value == 17, "Wrong nonstandard failure"); }
    ++nonstandard;
  });
  PumpUntil(storage, [&] { return failed && succeeded && nonstandard; });
  storage.Tick();
  Check(failed == 1 && succeeded == 1 && nonstandard == 1, "Outcome repeated");
  Check(storage.GetNumFinishedUpserts() == 1, "Failed outcomes counted as successful writes");
  Check(!storage.IsSettled(), "Failure was not retained");
}

void CaptureLifetime()
{
  auto database = std::make_shared<TestDatabase>();
  Async storage(database, nullptr, "", 1);
  auto payload = std::make_shared<int>(7);
  std::weak_ptr<int> capture = payload;
  bool called = false;
  storage.UpsertWithOutcome({7}, [&, payload](std::exception_ptr error) {
    Check(!error && *payload == 7, "Successful outcome lost its capture");
    Check(!storage.IsSettled(), "Executing outcome reported settled");
    called = true;
  });
  payload.reset();
  Check(!storage.IsSettled(), "Queued outcome reported settled");
  PumpUntil(storage, [&] { return called; });
  Check(!capture.expired(), "Prepared outcome capture was released too early");
  PumpUntil(storage, [&] { return storage.IsSettled(); });
  Check(capture.expired(), "Settled storage retained its outcome capture");
  Check(storage.GetNumFinishedUpserts() == 1, "Success count missing");
}

void ThrowingCallback(bool failedJob)
{
  auto database = std::make_shared<TestDatabase>();
  Async storage(database, nullptr, "", 1);
  unsigned first = 0, second = 0;
  storage.UpsertWithOutcome({failedJob ? -1 : 7}, [&](std::exception_ptr error) {
    Check(bool(error) == failedJob, "Throwing callback received wrong outcome");
    ++first;
    throw std::logic_error("owner callback failed");
  });
  storage.UpsertWithOutcome({8}, [&](std::exception_ptr error) {
    Check(!error, "Later successful callback inherited failure");
    ++second;
  });
  bool caught = false;
  try { PumpUntil(storage, [&] { return first != 0; }); }
  catch (const std::logic_error& error) {
    caught = std::string(error.what()) == "owner callback failed";
  }
  Check(caught, "Callback exception did not reach owner");
  PumpUntil(storage, [&] { return second != 0; });
  storage.Tick();
  Check(first == 1 && second == 1, "Callback throw lost or repeated an outcome");
  Check(storage.GetNumFinishedUpserts() == 1, "Throwing callback counted as successful");
  Check(!storage.IsSettled(), "Callback failure was not retained");
}

void RejectedSubmission()
{
  struct State { bool rejectCopy = false; unsigned called = 0; };
  struct Callback {
    std::shared_ptr<State> state;
    explicit Callback(std::shared_ptr<State> value) : state(std::move(value)) {}
    Callback(const Callback& other) : state(other.state) {
      if (state->rejectCopy) throw std::runtime_error("submission copy failed");
    }
    void operator()(std::exception_ptr) const { ++state->called; }
  };
  auto database = std::make_shared<TestDatabase>();
  Async storage(database, nullptr, "", 1);
  auto state = std::make_shared<State>();
  Storage::UpsertOutcomeCallback callback = Callback(state);
  state->rejectCopy = true;
  bool refused = false;
  try { storage.UpsertWithOutcome({7}, callback); }
  catch (const std::runtime_error& error) { refused = std::string(error.what()) == "submission copy failed"; }
  Check(refused, "Submission copy failure did not propagate");
  bool accepted = false;
  storage.UpsertWithOutcome({8}, [&](std::exception_ptr error) {
    Check(!error, "Next accepted submission failed");
    accepted = true;
  });
  PumpUntil(storage, [&] { return accepted; });
  storage.Tick();
  Check(database->calls == 1 && state->called == 0, "Rejected submission left a job or outcome");
  Check(storage.GetNumFinishedUpserts() == 1, "Rejected submission counted success");
  Check(!storage.IsSettled(), "Submission failure was not retained");
}
}

int main()
{
  try {
    Defaults();
    DistinctOutcomes();
    CaptureLifetime();
    ThrowingCallback(false);
    ThrowingCallback(true);
    RejectedSubmission();
    std::cout << checks << " save outcome checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Unexpected nonstandard exception\n";
    return 1;
  }
}
