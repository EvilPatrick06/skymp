// Thornswood #1715. Light plugins (an .esl, or the TES4 header flag 0x200)
// get no top byte of their own: the game gives their forms
// 0xFE000000 | (lightIndex << 12) | (id & 0xFFF), counting light plugins
// apart from full ones, each in load order. These tests build small plugins
// in memory, so they need no game data.
#include "FormCallbacks.h"
#include "FormDesc.h"
#include "MpObjectReference.h"
#include "WorldState.h"
#include "libespm/Browser.h"
#include "libespm/Combiner.h"
#include "libespm/Convert.h"
#include "libespm/FLST.h"
#include "libespm/LoadOrder.h"
#include "libespm/Utils.h"
#include <catch2/catch_all.hpp>
#include <memory>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;

namespace {

// Plugin bytes: a TES4 header naming masters, then top level groups.
void U16(std::string& s, uint16_t v)
{
  s.append(reinterpret_cast<const char*>(&v), 2);
}
void U32(std::string& s, uint32_t v)
{
  s.append(reinterpret_cast<const char*>(&v), 4);
}
std::string Field(const char* type, const std::string& data)
{
  std::string s(type, 4);
  U16(s, static_cast<uint16_t>(data.size()));
  return s + data;
}
std::string Z(const std::string& v)
{
  return v + std::string(1, '\0');
}
std::string Id(uint32_t v)
{
  std::string s;
  U32(s, v);
  return s;
}
std::string Record(const char* type, uint32_t flags, uint32_t id,
                   const std::string& fields)
{
  std::string s(type, 4);
  U32(s, static_cast<uint32_t>(fields.size()));
  U32(s, flags);
  U32(s, id);
  U32(s, 0);
  U16(s, 44);
  U16(s, 0);
  return s + fields;
}
std::string Group(const char* label, const std::string& records)
{
  std::string s("GRUP", 4);
  U32(s, static_cast<uint32_t>(24 + records.size()));
  s.append(label, 4);
  U32(s, 0);
  U32(s, 0);
  U32(s, 0);
  return s + records;
}
std::string Tes4(uint32_t flags, const std::vector<std::string>& masters)
{
  std::string hedr;
  float version = 1.7f;
  hedr.append(reinterpret_cast<const char*>(&version), 4);
  U32(hedr, 0);
  U32(hedr, 0x800);
  std::string fields = Field("HEDR", hedr);
  for (auto& m : masters) {
    fields += Field("MAST", Z(m)) + Field("DATA", std::string(8, '\0'));
  }
  return Record("TES4", flags, 0, fields);
}
std::string FormList(uint32_t id, const char* edid,
                     const std::vector<uint32_t>& entries)
{
  std::string fields = Field("EDID", Z(edid));
  for (auto e : entries) {
    fields += Field("LNAM", Id(e));
  }
  return Record("FLST", 0, id, fields);
}

// Owns the bytes and browsers a Combiner reads.
struct Plugins
{
  std::vector<std::unique_ptr<std::string>> bytes;
  std::vector<std::unique_ptr<espm::Browser>> browsers;
  espm::Combiner combiner;

  void Add(const char* fileName, std::string content)
  {
    bytes.push_back(std::make_unique<std::string>(std::move(content)));
    browsers.push_back(std::make_unique<espm::Browser>(
      bytes.back()->data(), bytes.back()->size()));
    combiner.AddSource(browsers.back().get(), fileName);
  }
};

std::string EditorId(const espm::LookupResult& r)
{
  espm::CompressedFieldsCache cache;
  return r.rec ? r.rec->GetEditorId(cache) : "";
}

}

TEST_CASE("IsLightPlugin is the game's rule", "[LightPlugins]")
{
  REQUIRE(espm::IsLightPlugin("KhisartinBeards.esp", 0x200));
  REQUIRE(espm::IsLightPlugin("Embers XD.esm", 0x201));
  REQUIRE(espm::IsLightPlugin("ccQDRSSE001-SurvivalMode.esl", 0));
  REQUIRE(espm::IsLightPlugin("UPPER.ESL", 0));
  REQUIRE_FALSE(espm::IsLightPlugin("Skyrim.esm", 0x1));
  REQUIRE_FALSE(espm::IsLightPlugin("Thornswood-Content.esp", 0));
  REQUIRE_FALSE(espm::IsLightPlugin("esl", 0));
}

TEST_CASE("PluginSlot gives the ids the game gives", "[LightPlugins]")
{
  using espm::PluginSlot;
  // KhisartinBeards.esp, 42nd light plugin on the main build on 5 Oct
  REQUIRE(PluginSlot{ true, 0x29 }.ToId(0x827) == 0xFE029827);
  REQUIRE(PluginSlot{ false, 0x2d }.ToId(0x19bad4) == 0x2d19bad4);
  REQUIRE(PluginSlot::Of(0xFE029838) == PluginSlot{ true, 0x29 });
  REQUIRE(PluginSlot::Of(0x0100122a) == PluginSlot{ false, 1 });
  REQUIRE_FALSE(PluginSlot::Of(0xFF000ded).has_value());
}

TEST_CASE("LoadOrder counts full and light plugins apart", "[LightPlugins]")
{
  using espm::PluginSlot;
  espm::LoadOrder lo({ "Skyrim.esm", "A.esl", "B.esp", "C.esp" },
                     { PluginSlot{ false, 0 }, PluginSlot{ true, 0 },
                       PluginSlot{ false, 1 }, PluginSlot{ true, 1 } });
  REQUIRE(lo.GetNumFullPlugins() == 2);
  REQUIRE(lo.GetNumLightPlugins() == 2);
  REQUIRE(lo.FindByFormId(0x01000800) == 2u);
  REQUIRE(lo.FindByFormId(0xFE001800) == 3u);
  REQUIRE_FALSE(lo.FindByFormId(0xFE002800).has_value());
  REQUIRE_FALSE(lo.FindByFormId(0x02000800).has_value());
  REQUIRE_FALSE(lo.FindByFormId(0xFF000800).has_value());

  // Slots the game would not give are refused.
  REQUIRE_THROWS(espm::LoadOrder({ "A.esp", "B.esp" },
                                 { PluginSlot{ false, 0 },
                                   PluginSlot{ false, 2 } }));
  REQUIRE_THROWS(espm::LoadOrder({ "A.esl" }, { PluginSlot{ true, 1 } }));
}

TEST_CASE("Combiner gives a light plugin's forms the game's ids, both ways",
          "[LightPlugins]")
{
  Plugins p;
  p.Add("Base.esm",
        Tes4(0x1, {}) +
          Group("FLST", FormList(0x00000800, "BaseList", {})));
  // Light by its flag: an own form 0x827 and a change to Base.esm's list.
  p.Add("Beards.esp",
        Tes4(0x200, { "Base.esm" }) +
          Group("FLST",
                FormList(0x01000827, "Beard", {}) +
                  FormList(0x00000800, "BaseListFromBeards",
                           { 0x01000827 })));
  // Full, with a light master: changes the beard and names it in a list.
  p.Add("Full.esp",
        Tes4(0, { "Base.esm", "Beards.esp" }) +
          Group("FLST",
                FormList(0x01000827, "BeardFromFull", {}) +
                  FormList(0x02000801, "FullList", { 0x01000827 })));
  // Light by its extension, after a full plugin: light index 1, not 2.
  p.Add("Late.esl",
        Tes4(0, { "Beards.esp" }) +
          Group("FLST", FormList(0x01000801, "Late", { 0x00000827 })));

  auto br = p.combiner.Combine();
  const auto& lo = br->GetLoadOrder();
  REQUIRE(lo.GetNumFullPlugins() == 2);
  REQUIRE(lo.GetNumLightPlugins() == 2);

  // The game's ids: Beards.esp is light index 0, Late.esl light index 1,
  // Full.esp full index 1.
  auto beard = br->LookupByIdAll(0xFE000827);
  REQUIRE(beard.size() == 2);
  REQUIRE(EditorId(beard[0]) == "Beard");
  REQUIRE(EditorId(beard[1]) == "BeardFromFull");
  REQUIRE(EditorId(br->LookupById(0x01000801)) == "FullList");
  REQUIRE(EditorId(br->LookupById(0xFE001801)) == "Late");
  REQUIRE(EditorId(br->LookupById(0x00000800)) == "BaseListFromBeards");

  // Ids written inside each file come out as the game's ids.
  espm::CompressedFieldsCache cache;
  auto fullList = br->LookupById(0x01000801);
  auto ids = espm::Convert<espm::FLST>(fullList.rec)->GetData(cache).formIds;
  REQUIRE(ids.size() == 1);
  REQUIRE(fullList.ToGlobalId(ids[0]) == 0xFE000827);
  auto late = br->LookupById(0xFE001801);
  ids = espm::Convert<espm::FLST>(late.rec)->GetData(cache).formIds;
  REQUIRE(ids.size() == 1);
  REQUIRE(late.ToGlobalId(ids[0]) == 0xFE000827);

  // And back: the game's id to the id each file writes, where it can.
  REQUIRE(espm::utils::GetMappedId(0xFE000827, *br->GetRawMapping(2)) ==
          0x01000827);
  REQUIRE(espm::utils::GetMappedId(0xFE000827, *br->GetRawMapping(1)) ==
          0x01000827);
  REQUIRE(espm::utils::GetMappedId(0xFE000827, *br->GetRawMapping(3)) ==
          0x00000827);
  // Base.esm cannot name a form of Beards.esp.
  REQUIRE(espm::utils::GetMappedId(0xFE000827, *br->GetRawMapping(0)) >=
          0xFF000000);

  // Descriptors
  REQUIRE(FormDesc::FromFormId(0xFE000827, lo).ToString() ==
          "827:Beards.esp");
  REQUIRE(FormDesc::FromString("801:Late.esl").ToFormId(lo) == 0xFE001801);
  REQUIRE(FormDesc::FromString("801:Full.esp").ToFormId(lo) == 0x01000801);
}

TEST_CASE("A light plugin with a form the game cannot tell apart is refused",
          "[LightPlugins]")
{
  Plugins p;
  p.Add("Wide.esp",
        Tes4(0x200, {}) + Group("FLST", FormList(0x00001800, "Wide", {})));
  try {
    p.combiner.Combine();
    FAIL("Combine accepted form 0x1800 in a light plugin");
  } catch (std::exception& e) {
    REQUIRE_THAT(e.what(), ContainsSubstring("Wide.esp"));
    REQUIRE_THAT(e.what(), ContainsSubstring("0x00001800"));
  }
}

TEST_CASE("GetAllForms finds a light plugin's forms by 0x100 and its light "
          "index",
          "[LightPlugins]")
{
  using espm::PluginSlot;
  WorldState worldState;
  worldState.espmFiles = espm::LoadOrder(
    { "Skyrim.esm", "Beards.esp", "Full.esp" },
    { PluginSlot{ false, 0 }, PluginSlot{ true, 0 },
      PluginSlot{ false, 1 } });
  for (uint32_t id : { 0x00000d62u, 0xFE000827u, 0xFE000838u, 0x01000801u,
                       0xFF000001u }) {
    worldState.AddForm(std::make_unique<MpObjectReference>(
                         LocationalData(), FormCallbacks::DoNothing(),
                         0x00000d62, "STAT"),
                       id);
  }
  // A full plugin and the server's own forms by their top byte, as before
  auto skyrim = worldState.GetAllForms(0);
  auto full = worldState.GetAllForms(1);
  auto created = worldState.GetAllForms(0xFF);
  // A light plugin by 0x100 + its light index
  auto beards = worldState.GetAllForms(0x100);
  auto noSuchLight = worldState.GetAllForms(0x101);
  REQUIRE(skyrim);
  REQUIRE(full);
  REQUIRE(created);
  REQUIRE(beards);
  REQUIRE(noSuchLight);
  REQUIRE(skyrim->size() == 1);
  REQUIRE(full->size() == 1);
  REQUIRE((*full)[0] == 0x01000801);
  REQUIRE(created->size() == 1);
  REQUIRE(beards->size() == 2);
  REQUIRE(noSuchLight->empty());
  // 0xFE names no plugin: every light plugin's forms have it
  REQUIRE_FALSE(worldState.GetAllForms(0xFE));
  REQUIRE_FALSE(worldState.GetAllForms(0x1100));
}
