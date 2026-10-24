#pragma once
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace PendingSaveDetail {
template <class Snapshot, class Origin>
struct Entry
{
  Snapshot snapshot;
  Origin origin;
};

template <class Snapshot, class Identity, class Origin>
struct Batch
{
  std::vector<std::optional<Snapshot>> rows;
  std::map<Identity, Origin> origins;

  void Swap(Batch& other) noexcept
  {
    static_assert(noexcept(rows.swap(other.rows)));
    static_assert(noexcept(origins.swap(other.origins)));
    rows.swap(other.rows);
    origins.swap(other.origins);
  }

  void Clear() noexcept
  {
    rows.clear();
    origins.clear();
  }
};

// Preparation copies into private storage; failure cannot consume an accepted
// pending/retry row. Instance ownership stays with its snapshot through retry.
template <class Snapshot, class Identity, class Origin>
Batch<Snapshot, Identity, Origin> Prepare(
  const std::map<Identity, Entry<Snapshot, Origin>>& pending,
  const Batch<Snapshot, Identity, Origin>& retry)
{
  Batch<Snapshot, Identity, Origin> candidate;
  if (retry.rows.size() > candidate.rows.max_size() ||
      pending.size() > candidate.rows.max_size() - retry.rows.size()) {
    throw std::length_error("Save batch exceeds its size limit");
  }
  candidate.rows.reserve(pending.size() + retry.rows.size());
  for (const auto& [identity, entry] : pending) {
    if (identity != entry.snapshot.formDesc) {
      throw std::runtime_error("Pending snapshot identity mismatch");
    }
    candidate.rows.push_back(entry.snapshot);
    candidate.origins.emplace(identity, entry.origin);
  }
  for (const auto& row : retry.rows) {
    if (!row || pending.count(row->formDesc)) continue;
    const auto& origin = retry.origins.at(row->formDesc);
    if (!candidate.origins.emplace(row->formDesc, origin).second) {
      throw std::runtime_error("Duplicate retry snapshot identity");
    }
    candidate.rows.push_back(*row);
  }
  return candidate;
}
}
