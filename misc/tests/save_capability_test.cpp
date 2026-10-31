#include "database_drivers/IDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using Database = Viet::IDatabase<int, int, std::vector<int>>;
using Storage = Viet::ISaveStorage<int, int, std::vector<int>>;
using Async = Viet::AsyncSaveStorage<int, int, std::vector<int>>;

class LegacyDatabase : public Database
{
public:
  std::atomic<unsigned> operations{0};
  void Iterate(const IterateCallback&, std::optional<std::vector<int>>) override
  { ++operations; throw std::runtime_error("Unexpected database operation"); }
protected:
  std::vector<std::optional<int>>&& UpsertImpl(
    std::vector<std::optional<int>>&&, size_t&) override
  { ++operations; throw std::runtime_error("Unexpected database operation"); }
};

class DeclaredDatabase : public LegacyDatabase
{
public:
  explicit DeclaredDatabase(bool value) : declared(value) {}
  bool declared;
  mutable unsigned queries = 0;
  bool SupportsDurableUpsert() const noexcept override
  { ++queries; return declared; }
};

class LegacyStorage : public Storage
{
public:
  void IterateSync(const IterateSyncCallback&) override {}
  void Upsert(std::vector<std::optional<int>>&&, const UpsertCallback&) override {}
  void Iterate(const IterateCallback&, const std::optional<std::vector<int>>&) override {}
  uint32_t GetNumFinishedUpserts() const override { return 0; }
  uint32_t GetNumFinishedIterates() const override { return 0; }
  void Tick() override {}
  bool GetRecycledChangeFormsBuffer(std::vector<std::optional<int>>&) override
  { return false; }
  const std::string& GetName() const override
  { static const std::string name = "legacy"; return name; }
};

unsigned checks = 0;
void Check(bool result, const char* message)
{
  ++checks;
  if (!result) throw std::runtime_error(message);
}
}

int main()
{
  static_assert(noexcept(std::declval<const Database&>().SupportsDurableUpsert()));
  static_assert(noexcept(std::declval<const Storage&>().SupportsDurableUpsert()));
  static_assert(noexcept(std::declval<const Async&>().SupportsDurableUpsert()));
  try {
    LegacyStorage legacyStorage;
    LegacyDatabase legacyDatabase;
    Check(!legacyStorage.SupportsDurableUpsert(), "Legacy storage must refuse");
    Check(!legacyDatabase.SupportsDurableUpsert(), "Legacy database must refuse");
    auto legacy = std::make_shared<LegacyDatabase>();
    {
      Async storage(legacy, nullptr, "file", 1);
      Check(!storage.SupportsDurableUpsert(), "Backend name must not imply support");
      Check(legacy->operations == 0, "Capability query must not dispatch I/O");
    }
    for (bool declared : {false, true}) {
      auto backend = std::make_shared<DeclaredDatabase>(declared);
      {
        Async storage(backend, nullptr, "", 1);
        Check(backend->queries == 1, "Backend must be queried once in construction");
        Check(storage.SupportsDurableUpsert() == declared, "Explicit guarantee must propagate");
        backend->declared = !declared;
        Check(storage.SupportsDurableUpsert() == declared, "Guarantee must stay frozen");
        Check(storage.SupportsDurableUpsert() == declared, "Repeated query must stay frozen");
        Check(backend->queries == 1, "Storage queries must not call the backend again");
      }
      Check(backend->operations == 0, "Capability construction must not dispatch I/O");
    }
    {
      Async storage(nullptr, nullptr, "file", 1);
      Check(!storage.SupportsDurableUpsert(), "Null backend cannot promise durability");
    }
    std::cout << checks << " storage capability checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
