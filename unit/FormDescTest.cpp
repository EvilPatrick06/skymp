#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

TEST_CASE("ToString/FromString", "[FormDesc]")
{
  REQUIRE(FormDesc(0xAAA, "").ToString() == "aaa");
  REQUIRE(FormDesc(0xAAA, "Skyrim.esm").ToString() == "aaa:Skyrim.esm");

  auto x = FormDesc::FromString("aaa");
  REQUIRE(x.file == "");
  REQUIRE(x.shortFormId == 0xAAA);

  auto v = FormDesc::FromString("aaa:Skyrim.esm");
  REQUIRE(v.file == "Skyrim.esm");
  REQUIRE(v.shortFormId == 0xAAA);
}

TEST_CASE("ToFormId/FromFormId", "[FormDesc]")
{
  auto list = espm::LoadOrder::FullPlugins({ "Skyrim.esm", "Update.esm" });

  REQUIRE(FormDesc::FromFormId(0x01000001, list) ==
          FormDesc(0x1, "Update.esm"));
  REQUIRE(FormDesc::FromFormId(0x00000001, list) ==
          FormDesc(0x1, "Skyrim.esm"));
  REQUIRE(FormDesc::FromFormId(0xff000bbb, list) == FormDesc(0xbbb, ""));

  REQUIRE(FormDesc(0x1, "Update.esm").ToFormId(list) == 0x01000001);
  REQUIRE(FormDesc(0x1, "Skyrim.esm").ToFormId(list) == 0x00000001);
  REQUIRE(FormDesc(0x1, "").ToFormId(list) == 0xff000001);
}

// Thornswood #1715. A light plugin's forms are 0xFE000000 | (lightIndex <<
// 12) | id, with light and full plugins counted apart, each in load order.
TEST_CASE("ToFormId/FromFormId with light plugins", "[FormDesc]")
{
  using espm::PluginSlot;
  espm::LoadOrder list(
    { "Skyrim.esm", "A.esl", "B.esp", "C.esp", "D.esp" },
    { PluginSlot{ false, 0 }, PluginSlot{ true, 0 }, PluginSlot{ false, 1 },
      PluginSlot{ true, 1 }, PluginSlot{ false, 2 } });

  REQUIRE(FormDesc::FromFormId(0xfe000801, list) ==
          FormDesc(0x801, "A.esl"));
  REQUIRE(FormDesc::FromFormId(0xfe001827, list) ==
          FormDesc(0x827, "C.esp"));
  REQUIRE(FormDesc::FromFormId(0x01000001, list) == FormDesc(0x1, "B.esp"));
  REQUIRE(FormDesc::FromFormId(0x02000001, list) == FormDesc(0x1, "D.esp"));

  REQUIRE(FormDesc(0x801, "A.esl").ToFormId(list) == 0xfe000801);
  REQUIRE(FormDesc(0x827, "C.esp").ToFormId(list) == 0xfe001827);
  REQUIRE(FormDesc(0x1, "B.esp").ToFormId(list) == 0x01000001);
  REQUIRE(FormDesc(0x1, "D.esp").ToFormId(list) == 0x02000001);

  // No light plugin with index 2, and no full one with index 3
  REQUIRE_THROWS(FormDesc::FromFormId(0xfe002801, list));
  REQUIRE_THROWS(FormDesc::FromFormId(0x03000001, list));
  // A light plugin's ids have 12 bits
  REQUIRE_THROWS(FormDesc(0x1801, "A.esl").ToFormId(list));
}
