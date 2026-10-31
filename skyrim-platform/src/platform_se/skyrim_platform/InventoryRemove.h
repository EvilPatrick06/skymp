#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// THORNSWOOD (Thornswood #2146). Which of an inventory entry's extra lists a
// removal takes away, and how many units of each.
//
// The client picks the lists (applyInventory's takeAway in sync/inventory.ts)
// out of getExtraContainerChanges, which lists them in the order ListsOf
// (InventoryApi.cpp) does, and says how many lists there were when it read
// them. removeInventoryItems resolves the picks against the lists as they are
// when it is called and again when its queued work runs. When the count of
// lists is not the one the client read, an index is out of range or picked
// twice, or a list holds fewer units than picked, the inventory is not the
// one the client picked from: nothing is resolved and nothing is taken away,
// and the client's next pass reads it again.
namespace InventoryRemove {

using Pick = std::pair<uint32_t, int32_t>; // index, units

template <class List, class CountOf>
std::optional<std::vector<std::pair<List*, int32_t>>> Resolve(
  const std::vector<List*>& lists, size_t listCount,
  const std::vector<Pick>& picks, CountOf countOf)
{
  if (lists.size() != listCount) {
    return std::nullopt;
  }

  std::vector<bool> picked(lists.size(), false);
  std::vector<std::pair<List*, int32_t>> res;
  for (const auto& [index, units] : picks) {
    if (index >= lists.size() || picked[index] || !lists[index] ||
        units <= 0 || units > countOf(lists[index])) {
      return std::nullopt;
    }
    picked[index] = true;
    res.push_back({ lists[index], units });
  }
  return res;
}

}
