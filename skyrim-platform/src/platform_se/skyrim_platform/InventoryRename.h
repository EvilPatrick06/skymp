#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// THORNSWOOD (Thornswood #1651). Which of an inventory entry's extra lists a
// rename reaches.
//
// The server can change the name of a piece somebody already holds: a
// Thornswood piece carries its condition on the end of its name, "Iron Sword
// (85%)", and a hit, a repair or a chest's dull clock writes a new one. The
// client used to apply a difference in names by taking the piece away and
// adding one under the new name, which takes a worn piece off the body.
// renameInventoryItems (InventoryApi.cpp) renames the piece where it lies
// instead, and this picks the lists it renames.
//
// A list is a candidate when its name is `from` (an empty `from` is a list
// with no name of its own). nameOf returns nothing for a list whose name is
// not a custom one (a quest's alias name), which is never renamed. Worn lists
// come first, so of two pieces with one name the worn one is renamed: the
// server renames the piece a hit reached, and a hit reaches what is worn.
// Lists are taken while their counts fit; when they cannot make exactly
// `count`, nothing is chosen and the caller applies the change the old way.
namespace InventoryRename {

template <class List, class NameOf, class CountOf, class IsWorn>
std::vector<List*> Select(const std::vector<List*>& lists,
                          const std::string& from, int32_t count,
                          NameOf nameOf, CountOf countOf, IsWorn isWorn)
{
  std::vector<List*> chosen;
  if (count <= 0) {
    return chosen;
  }

  std::vector<List*> candidates;
  for (int pass = 0; pass < 2; ++pass) {
    const bool wantWorn = pass == 0;
    for (List* list : lists) {
      if (!list || isWorn(list) != wantWorn) {
        continue;
      }
      std::optional<std::string> name = nameOf(list);
      if (name && *name == from) {
        candidates.push_back(list);
      }
    }
  }

  int32_t remaining = count;
  for (List* list : candidates) {
    const int32_t n = countOf(list);
    if (n > 0 && n <= remaining) {
      chosen.push_back(list);
      remaining -= n;
      if (remaining == 0) {
        return chosen;
      }
    }
  }
  return {};
}

}
