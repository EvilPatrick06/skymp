// Thornswood #2146. InventoryRemove::Resolve, the lists removeInventoryItems
// (InventoryApi.cpp) takes away, over stand-in lists that hold a count.
#include "InventoryRemove.h"
#include <cassert>
#include <iostream>
#include <vector>

struct List
{
  int count;
};

static auto resolve(const std::vector<List*>& lists, size_t listCount,
                    const std::vector<InventoryRemove::Pick>& picks)
{
  return InventoryRemove::Resolve(lists, listCount, picks,
                                  [](List* l) { return l->count; });
}

int main()
{
  List tempered{ 1 }, plain{ 1 }, stack{ 3 };
  const std::vector<List*> lists{ &plain, &tempered, &stack };

  // The tempered boots, by their own list.
  auto one = resolve(lists, 3, { { 1, 1 } });
  assert(one && one->size() == 1 && (*one)[0].first == &tempered &&
         (*one)[0].second == 1);

  // Part of a stack, and two lists at once.
  auto two = resolve(lists, 3, { { 2, 2 }, { 0, 1 } });
  assert(two && two->size() == 2 && (*two)[0].first == &stack &&
         (*two)[0].second == 2 && (*two)[1].first == &plain);

  // Nothing picked is a removal of plain units only, not a failure.
  auto none = resolve(lists, 3, {});
  assert(none && none->empty());

  // The lists are not the ones the client read: nothing at all.
  assert(!resolve(lists, 2, { { 1, 1 } }));
  assert(!resolve({ &plain, &tempered }, 3, { { 1, 1 } }));
  // An index past the end, one picked twice, more than a list holds, or no
  // units: nothing at all.
  assert(!resolve(lists, 3, { { 3, 1 } }));
  assert(!resolve(lists, 3, { { 1, 1 }, { 1, 1 } }));
  assert(!resolve(lists, 3, { { 2, 4 } }));
  assert(!resolve(lists, 3, { { 2, 0 } }));
  assert(!resolve(lists, 3, { { 0, 1 }, { 2, 5 } }));

  std::cout << "PASS remove by the game's own lists: picked lists and units, "
               "and nothing when the lists are not the ones read\n";
}
