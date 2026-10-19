#include <catch2/catch_all.hpp>

#include "OverEncumberedIgnores.h"

// THORNSWOOD PATCH (Thornswood #1385): the characters the game does not hold
// to their carry weight. 0x14 is the person's own character.

namespace {
constexpr uint32_t kPlayer = 0x14;
constexpr uint32_t kOther = 0xff000a2c;
}

TEST_CASE("An empty list holds everybody to their carry weight",
          "[OverEncumberedIgnores]")
{
  OverEncumberedIgnores l;
  REQUIRE(l.Size() == 0);
  REQUIRE_FALSE(l.Has(kPlayer));
  REQUIRE_FALSE(l.Has(kOther));
}

TEST_CASE("A form on the list is not held, and nobody else changes",
          "[OverEncumberedIgnores]")
{
  OverEncumberedIgnores l;
  l.Set(kPlayer, true);
  REQUIRE(l.Has(kPlayer));
  REQUIRE_FALSE(l.Has(kOther));
  REQUIRE(l.Size() == 1);
}

TEST_CASE("Setting it twice keeps one entry, and false takes it off",
          "[OverEncumberedIgnores]")
{
  OverEncumberedIgnores l;
  l.Set(kPlayer, true);
  l.Set(kPlayer, true);
  REQUIRE(l.Size() == 1);
  l.Set(kPlayer, false);
  REQUIRE_FALSE(l.Has(kPlayer));
  REQUIRE(l.Size() == 0);
  // Taking off a form that is not there changes nothing.
  l.Set(kOther, false);
  REQUIRE(l.Size() == 0);
}

TEST_CASE("Clear empties the list", "[OverEncumberedIgnores]")
{
  OverEncumberedIgnores l;
  l.Set(kPlayer, true);
  l.Set(kOther, true);
  l.Clear();
  REQUIRE(l.Size() == 0);
  REQUIRE_FALSE(l.Has(kPlayer));
  REQUIRE_FALSE(l.Has(kOther));
}
