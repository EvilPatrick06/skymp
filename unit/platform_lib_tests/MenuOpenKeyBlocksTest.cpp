#include <catch2/catch_all.hpp>

#include "MenuOpenKeyBlocks.h"

#include <set>
#include <string>

// THORNSWOOD PATCH (Thornswood #1016): the list of keys the game's menu
// opener does not see. J is DirectX scan code 36, Escape 1, K 37.

namespace {
constexpr uint32_t kJ = 36;
constexpr uint32_t kK = 37;
constexpr uint32_t kEsc = 1;

MenuOpenKeyBlocks::IsMenuOpen Menus(std::set<std::string> open)
{
  return [open](const std::string& name) { return open.count(name) > 0; };
}
}

TEST_CASE("An empty list blocks nothing", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  REQUIRE(b.Size() == 0);
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({})));
}

TEST_CASE("J as Journal is kept from the game in the open world",
          "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true, { "InventoryMenu", "Journal Menu" });
  REQUIRE(b.ShouldBlock("Journal", true, kJ, Menus({})));
  // The HUD and the cursor are not in the except list.
  REQUIRE(b.ShouldBlock("Journal", true, kJ,
                        Menus({ "HUD Menu", "Cursor Menu" })));
}

TEST_CASE("Escape still opens the System page", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true);
  REQUIRE_FALSE(b.ShouldBlock("Pause", true, kEsc, Menus({})));
}

TEST_CASE("A rebind on either side keeps working", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true);
  // The game's journal moved to K: K opens it as before.
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kK, Menus({})));
  // J is something else to the game now: not ours to keep.
  REQUIRE_FALSE(b.ShouldBlock("Quick Stats", true, kJ, Menus({})));
  REQUIRE_FALSE(b.ShouldBlock("", true, kJ, Menus({})));
}

TEST_CASE("Only the keyboard is kept", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true);
  REQUIRE_FALSE(b.ShouldBlock("Journal", false, kJ, Menus({})));
}

TEST_CASE("An open except menu gives the game the key",
          "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true, { "InventoryMenu", "Journal Menu" });
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({ "InventoryMenu" })));
  // The book Escape opened still closes on J.
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({ "Journal Menu" })));
  REQUIRE(b.ShouldBlock("Journal", true, kJ, Menus({ "MapMenu" })));
}

TEST_CASE("Set again replaces, false removes", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true, { "InventoryMenu" });
  b.Set("Journal", kJ, true, { "MapMenu" });
  REQUIRE(b.Size() == 1);
  REQUIRE(b.ShouldBlock("Journal", true, kJ, Menus({ "InventoryMenu" })));
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({ "MapMenu" })));

  b.Set("Journal", kJ, false);
  REQUIRE(b.Size() == 0);
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({})));

  // Removing what is not there is not an error.
  b.Set("Journal", kJ, false);
  REQUIRE(b.Size() == 0);
}

TEST_CASE("Entries are kept apart by key and name", "[MenuOpenKeyBlocks]")
{
  MenuOpenKeyBlocks b;
  b.Set("Journal", kJ, true);
  b.Set("Quick Magic", 25, true);
  REQUIRE(b.Size() == 2);
  b.Set("Journal", kJ, false);
  REQUIRE_FALSE(b.ShouldBlock("Journal", true, kJ, Menus({})));
  REQUIRE(b.ShouldBlock("Quick Magic", true, 25, Menus({})));
  b.Clear();
  REQUIRE(b.Size() == 0);
}
