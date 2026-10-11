// Thornswood #1651. InventoryRename::Select, the choice of extra lists that
// renameInventoryItems (InventoryApi.cpp) renames in place, over stand-in
// lists: a name (or none of its own, or one a quest owns), a count, worn.
#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include "InventoryRename.h"

struct List
{
  std::optional<std::string> name; // nullopt: a name a quest owns
  int count;
  bool worn;
};

static std::vector<List*> pick(const std::vector<List*>& lists,
                               const std::string& from, int count)
{
  return InventoryRename::Select(
    lists, from, count, [](List* l) { return l->name; },
    [](List* l) { return l->count; }, [](List* l) { return l->worn; });
}

int main()
{
  // One worn sword at 85%: renamed where it lies.
  List worn{ std::string("Iron Sword (85%)"), 1, true };
  assert((pick({ &worn }, "Iron Sword (85%)", 1) == std::vector<List*>{ &worn }));

  // A name that is not there, or a count that is not, chooses nothing.
  assert(pick({ &worn }, "Iron Sword (84%)", 1).empty());
  assert(pick({ &worn }, "Iron Sword (85%)", 2).empty());
  assert(pick({ &worn }, "Iron Sword (85%)", 0).empty());

  // Two pieces of one name, one worn and one in the pack: the worn one, the
  // piece a hit reaches.
  List spare{ std::string("Iron Sword (85%)"), 1, false };
  assert((pick({ &spare, &worn }, "Iron Sword (85%)", 1) ==
          std::vector<List*>{ &worn }));
  // Both, when the server renamed both.
  assert((pick({ &spare, &worn }, "Iron Sword (85%)", 2) ==
          std::vector<List*>{ &worn, &spare }));

  // A stack of three in a chest, of which the server renamed one: no list
  // holds exactly one, so nothing is chosen and the client takes one away
  // and adds one, which is right for a piece nobody wears.
  List stack{ std::string("Steel Dagger (60%)"), 3, false };
  assert(pick({ &stack }, "Steel Dagger (60%)", 1).empty());
  assert((pick({ &stack }, "Steel Dagger (60%)", 3) ==
          std::vector<List*>{ &stack }));

  // A list with no name of its own answers to the empty name.
  List plain{ std::string(), 1, true };
  assert((pick({ &plain }, "", 1) == std::vector<List*>{ &plain }));
  assert(pick({ &plain }, "Iron Sword", 1).empty());

  // A name a quest owns is never renamed.
  List quest{ std::nullopt, 1, true };
  assert(pick({ &quest }, "", 1).empty());

  // Null lists are passed over.
  assert((pick({ nullptr, &worn }, "Iron Sword (85%)", 1) ==
          std::vector<List*>{ &worn }));

  std::cout << "PASS rename in place: the worn piece first, exact counts or "
               "nothing, no name of its own, and quest names left alone\n";
}
