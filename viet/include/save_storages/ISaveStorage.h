#pragma once
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Viet {

template <typename T, typename FormDescType, typename FilterType>
class ISaveStorage
{
public:
  using IterateSyncCallback = std::function<void(const T&)>;
  using UpsertCallback = std::function<void()>;
  using UpsertOutcomeCallback = std::function<void(std::exception_ptr)>;
  using IterateCallback = std::function<void(const std::vector<T>&)>;

  virtual ~ISaveStorage() = default;

  // Static, side-effect-free opt-in: a successful whole-batch acknowledgement
  // follows checked persistence barriers. Failures must not be acknowledged.
  // This does not certify current queue health or a stopped process.
  virtual bool SupportsDurableUpsert() const noexcept { return false; }

  // Instantaneous owner-thread observation; the caller excludes new producers.
  // This does not certify durability, restoration or a stopped process.
  virtual bool IsSettled() const { return false; }

  // Opt-in to one owner-thread outcome for this exact submission. Null error
  // means the database returned, not durable persistence. A submission throw
  // leaves no accepted job or future outcome; inline outcomes are provisional
  // until the submission call returns. Legacy implementations must refuse.
  virtual bool SupportsUpsertOutcomes() const noexcept { return false; }
  virtual void UpsertWithOutcome(std::vector<std::optional<T>>&&,
                                const UpsertOutcomeCallback&)
  {
    throw std::runtime_error("Save storage does not support correlated outcomes");
  }

  virtual void IterateSync(const IterateSyncCallback& cb) = 0;
  virtual void Upsert(std::vector<std::optional<T>>&& changeForms,
                      const UpsertCallback& cb) = 0;
  virtual void Iterate(const IterateCallback& cb,
                       const std::optional<FilterType>& filter) = 0;
  virtual uint32_t GetNumFinishedUpserts() const = 0;
  virtual uint32_t GetNumFinishedIterates() const = 0;

  // Calls all callbacks that are ready to be called.
  // Basic exception guarantee: if a callback throws, it'll be deleted.
  // Other callbacks will still be called next time Tick is called.
  virtual void Tick() = 0;

  virtual bool GetRecycledChangeFormsBuffer(
    std::vector<std::optional<T>>& changeForms) = 0;

  virtual const std::string& GetName() const = 0;
};

namespace ISaveStorageUtils {
template <typename T, typename FormDescType, typename FilterType>
inline uint32_t CountSync(ISaveStorage<T, FormDescType, FilterType>& storage)
{
  uint32_t n = 0;
  storage.IterateSync([&](auto) { ++n; });
  return n;
}

template <typename T, typename FormDescType, typename FilterType>
inline std::optional<T> FindSync(
  ISaveStorage<T, FormDescType, FilterType>& storage,
  const FormDescType& formDesc)
{
  std::optional<T> res;
  storage.IterateSync([&](const T& changeForm) {
    if (changeForm.formDesc == formDesc)
      res = changeForm;
  });
  return res;
}

template <typename T, typename FormDescType, typename FilterType>
inline std::map<FormDescType, T> FindAllSync(
  ISaveStorage<T, FormDescType, FilterType>& storage)
{
  std::map<FormDescType, T> res;
  storage.IterateSync(
    [&](const T& changeForm) { res[changeForm.formDesc] = changeForm; });
  return res;
}
}

}
