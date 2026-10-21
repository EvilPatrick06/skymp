#include "Inventory.h"
#include <catch2/catch_all.hpp>

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
