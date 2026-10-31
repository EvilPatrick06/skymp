#include "Inventory.h"
#include <catch2/catch_all.hpp>
#include <limits>
#include <stdexcept>

TEST_CASE(
  "RemoveItems in case of multiple entries with the same baseId/extras",
  "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 10));
  inv.entries.push_back(Inventory::Entry(0xf, 10));

  inv.RemoveItems({ Inventory::Entry(0xf, 5) });

  REQUIRE(inv.entries.size() == 2);
  REQUIRE(inv.entries[0].count == 5);
  REQUIRE(inv.entries[1].count == 10);
}

TEST_CASE("Similar entries should be merged if needed to remove items",
          "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 10));
  inv.entries.push_back(Inventory::Entry(0xf, 10));

  inv.RemoveItems({ Inventory::Entry(0xf, 15) });

  REQUIRE(inv.entries.size() == 1);
  REQUIRE(inv.entries[0].count == 5);
}

TEST_CASE("Remove all items from similar entries", "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 10));
  inv.entries.push_back(Inventory::Entry(0xf, 10));

  inv.RemoveItems({ Inventory::Entry(0xf, 20) });

  REQUIRE(inv.entries.size() == 0);
}

TEST_CASE("Remove all items from a single entry", "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 10));

  inv.RemoveItems({ Inventory::Entry(0xf, 10) });

  REQUIRE(inv.entries.size() == 0);
}

TEST_CASE("Not enough items to remove", "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 10));
  inv.entries.push_back(Inventory::Entry(0xf, 9));

  REQUIRE_THROWS_AS(inv.RemoveItems({ Inventory::Entry(0xf, 20) }),
                    std::runtime_error);
}

TEST_CASE("AddItems adds every entry after one that stacks", "[Inventory]")
{
  Inventory inv;
  inv.AddItem(0xa, 1);

  inv.AddItems({ Inventory::Entry(0xa, 1), Inventory::Entry(0xb, 1),
                 Inventory::Entry(0xc, 1) });

  REQUIRE(inv.entries.size() == 3);
  REQUIRE(inv.GetItemCount(0xa) == 2);
  REQUIRE(inv.GetItemCount(0xb) == 1);
  REQUIRE(inv.GetItemCount(0xc) == 1);
}

TEST_CASE("AddItems stacks entries that repeat within one call", "[Inventory]")
{
  Inventory inv;

  inv.AddItems({ Inventory::Entry(0xa, 2), Inventory::Entry(0xb, 1),
                 Inventory::Entry(0xa, 3) });

  REQUIRE(inv.entries.size() == 2);
  REQUIRE(inv.GetItemCount(0xa) == 5);
  REQUIRE(inv.GetItemCount(0xb) == 1);
}

TEST_CASE("AddItems refuses a stack past its limit and changes nothing",
          "[Inventory]")
{
  constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
  Inventory inv;
  inv.AddItem(0xa, kMax - 1);

  REQUIRE(inv.CanAddItems({ Inventory::Entry(0xa, 1) }));
  REQUIRE_FALSE(
    inv.CanAddItems({ Inventory::Entry(0xb, 1), Inventory::Entry(0xa, 2) }));
  REQUIRE_THROWS_AS(
    inv.AddItems({ Inventory::Entry(0xb, 1), Inventory::Entry(0xa, 2) }),
    std::runtime_error);

  REQUIRE(inv.entries.size() == 1);
  REQUIRE(inv.GetItemCount(0xa) == kMax - 1);
  REQUIRE(inv.GetItemCount(0xb) == 0);

  inv.AddItems({ Inventory::Entry(0xa, 1) });
  REQUIRE(inv.GetItemCount(0xa) == kMax);
}

namespace {
Inventory::Entry Named(uint32_t baseId, uint32_t count, const char* name)
{
  Inventory::ExtraData extra;
  extra.name = name;
  return Inventory::Entry(baseId, count, extra);
}
}

TEST_CASE("ResolveRemoval takes the plain stack first, then stacks that "
          "differ only in their name, in order (Thornswood #3163)",
          "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Named(0xf, 2, "Venison (6 days left)"));
  inv.entries.push_back(Inventory::Entry(0xf, 1));
  inv.entries.push_back(Named(0xf, 5, "Venison (3 days left)"));
  const Inventory before = inv;

  auto taken = inv.ResolveRemoval({ Inventory::Entry(0xf, 4) });

  REQUIRE(inv == before);
  REQUIRE(taken.size() == 3);
  REQUIRE(taken[0] == Inventory::Entry(0xf, 1));
  REQUIRE(taken[1] == Named(0xf, 2, "Venison (6 days left)"));
  REQUIRE(taken[2] == Named(0xf, 1, "Venison (3 days left)"));

  inv.RemoveItems(taken);
  REQUIRE(inv.entries.size() == 1);
  REQUIRE(inv.entries[0] == Named(0xf, 4, "Venison (3 days left)"));
}

TEST_CASE("ResolveRemoval never takes a stack that differs in more than its "
          "name (Thornswood #3163)",
          "[Inventory]")
{
  Inventory inv;
  Inventory::ExtraData tempered;
  tempered.health = 1.1f;
  inv.entries.push_back(Inventory::Entry(0xf, 3, tempered));
  Inventory::Entry worn = Named(0xf, 1, "Iron Sword (85%)");
  worn.SetWorn(Inventory::Worn::Right);
  inv.entries.push_back(worn);
  inv.entries.push_back(Named(0xf, 1, "Iron Sword (90%)"));

  REQUIRE_THROWS_AS(inv.ResolveRemoval({ Inventory::Entry(0xf, 2) }),
                    std::runtime_error);
  auto taken = inv.ResolveRemoval({ Inventory::Entry(0xf, 1) });
  REQUIRE(taken.size() == 1);
  REQUIRE(taken[0] == Named(0xf, 1, "Iron Sword (90%)"));
}

TEST_CASE("ResolveRemoval counts one item named twice against the same "
          "stacks, and an exact named entry takes its own stack first "
          "(Thornswood #3163)",
          "[Inventory]")
{
  Inventory inv;
  inv.entries.push_back(Inventory::Entry(0xf, 1));
  inv.entries.push_back(Named(0xf, 2, "Bread (12 days left)"));

  REQUIRE_THROWS_AS(
    inv.ResolveRemoval({ Inventory::Entry(0xf, 2), Inventory::Entry(0xf, 2) }),
    std::runtime_error);

  auto taken = inv.ResolveRemoval(
    { Named(0xf, 1, "Bread (12 days left)"), Inventory::Entry(0xf, 2) });
  REQUIRE(taken.size() == 2);
  REQUIRE(taken[0] == Named(0xf, 2, "Bread (12 days left)"));
  REQUIRE(taken[1] == Inventory::Entry(0xf, 1));
}
