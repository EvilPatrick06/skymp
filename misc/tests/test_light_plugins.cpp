// Thornswood #1715. The server gives every form of a light plugin the id the
// game gives it, 0xFE000000 | (lightIndex << 12) | id, and converts those ids
// to and from "id:file" descriptors. Run by test_light_plugins.ps1 against the
// game's Data folder and the client's own load order lists from a session.
//
// Arguments: <Data> <Plugins.txt> <Skyrim.ccc> <client load order>
//            <client light plugin list> <scratch folder>
// The two client lists are what thornswood-front.js writes from
// Game.getModName and Game.getLightModName, so they are the game's own answer
// to which plugin is light and where it sits.
#include "FormDesc.h"
#include "libespm/COBJ.h"
#include "libespm/Combiner.h"
#include "libespm/Convert.h"
#include "libespm/FLST.h"
#include "libespm/GroupUtils.h"
#include "libespm/Loader.h"
#include "libespm/REFR.h"
#include "libespm/Utils.h"

#if __has_include("libespm/LoadOrder.h")
#  define THORNSWOOD_LIGHT_PLUGINS 1
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;

static void Check(bool ok, const std::string& what)
{
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

static std::string Lower(std::string s)
{
  for (auto& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

static std::string Hex(uint32_t v)
{
  char b[16];
  std::snprintf(b, sizeof(b), "%#x", v);
  return b;
}

static std::vector<std::string> ReadLines(const fs::path& p)
{
  std::vector<std::string> res;
  std::ifstream f(p);
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty()) {
      res.push_back(line);
    }
  }
  return res;
}

// What the server converts descriptors through: its load order with
// lightness, or before #1715 the bare list of file names.
#ifdef THORNSWOOD_LIGHT_PLUGINS
static const espm::LoadOrder& ServerLoadOrder(const espm::Loader& l)
{
  return l.GetLoadOrder();
}
#else
static std::vector<std::string> ServerLoadOrder(const espm::Loader& l)
{
  return l.GetFileNames();
}
#endif

// Which plugins the server holds as full and which as light, in order.
static void ServerLists(const espm::Loader& l, std::vector<std::string>& full,
                        std::vector<std::string>& light)
{
#ifdef THORNSWOOD_LIGHT_PLUGINS
  const auto& lo = l.GetLoadOrder();
  for (size_t i = 0; i < lo.size(); ++i) {
    (lo.GetSlot(i).light ? light : full).push_back(lo[i]);
  }
#else
  // Before #1715 every plugin took the next top byte, as a full plugin does.
  full = l.GetFileNames();
#endif
}

// A plugin file built in memory: a TES4 header naming masters, then records.
struct Fixture
{
  static void U16(std::string& s, uint16_t v)
  {
    s.append(reinterpret_cast<const char*>(&v), 2);
  }
  static void U32(std::string& s, uint32_t v)
  {
    s.append(reinterpret_cast<const char*>(&v), 4);
  }
  static std::string Field(const char* type, const std::string& data)
  {
    std::string s(type, 4);
    U16(s, static_cast<uint16_t>(data.size()));
    return s + data;
  }
  static std::string Id(uint32_t v)
  {
    std::string s;
    U32(s, v);
    return s;
  }
  static std::string Z(const std::string& v) { return v + std::string(1, '\0'); }
  static std::string Record(const char* type, uint32_t flags, uint32_t id,
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
  static std::string Group(const char* label, const std::string& records)
  {
    std::string s("GRUP", 4);
    U32(s, static_cast<uint32_t>(24 + records.size()));
    s.append(label, 4);
    U32(s, 0);
    U32(s, 0);
    U32(s, 0);
    return s + records;
  }
  static std::string Tes4(uint32_t flags,
                          const std::vector<std::string>& masters)
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
  static fs::path Write(const fs::path& dir, const std::string& name,
                        const std::string& bytes)
  {
    auto p = dir / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
  }
};

int main(int argc, char** argv)
{
  if (argc < 7) {
    std::printf("FAIL  usage: <Data> <Plugins.txt> <Skyrim.ccc> <client load "
                "order> <client light list> <scratch>\n");
    return 2;
  }
  const fs::path data = argv[1];
  const fs::path scratch = argv[6];
  fs::create_directories(scratch);

  // The load order the game builds: the five base masters, the Creation Club
  // plugins Skyrim.ccc names that are in Data, then every active line of
  // Plugins.txt.
  std::map<std::string, std::string> onDisk;
  for (auto& e : fs::directory_iterator(data)) {
    onDisk[Lower(e.path().filename().string())] =
      e.path().filename().string();
  }
  std::vector<std::string> order;
  std::set<std::string> listed;
  auto add = [&](const std::string& name) {
    auto it = onDisk.find(Lower(name));
    if (it != onDisk.end() && listed.insert(Lower(name)).second) {
      order.push_back(it->second);
    }
  };
  for (auto n : { "Skyrim.esm", "Update.esm", "Dawnguard.esm",
                  "HearthFires.esm", "Dragonborn.esm" }) {
    add(n);
  }
  for (auto& n : ReadLines(argv[3])) {
    add(n);
  }
  for (auto& n : ReadLines(argv[2])) {
    if (n[0] == '*') {
      add(n.substr(1));
    }
  }

  // A full plugin whose master is light: no plugin in the collection is one,
  // so it is built here. It changes the beard head part 0x827 of
  // KhisartinBeards.esp and lists it in a form list of its own.
  const auto lightMasterUser = Fixture::Write(
    scratch, "ThornswoodFullOverLight.esp",
    Fixture::Tes4(0, { "Skyrim.esm", "KhisartinBeards.esp" }) +
      Fixture::Group(
        "HDPT",
        Fixture::Record("HDPT", 0, 0x01000827,
                        Fixture::Field("EDID", Fixture::Z("FixtureBeard")))) +
      Fixture::Group(
        "FLST",
        Fixture::Record("FLST", 0, 0x02000800,
                        Fixture::Field("EDID", Fixture::Z("FixtureList")) +
                          Fixture::Field("LNAM", Fixture::Id(0x01000827)))));

  std::vector<fs::path> paths;
  for (auto& n : order) {
    paths.push_back(data / n);
  }
  paths.push_back(lightMasterUser);

  espm::Loader loader(paths, nullptr,
                      espm::Loader::BufferType::AllocatedBuffer);
  const auto& br = loader.GetBrowser();
  auto& cache = br.GetCache();
  const auto& lo = ServerLoadOrder(loader);

  // 1. The server's lists are the game's lists.
  std::vector<std::string> full, light;
  ServerLists(loader, full, light);
  full.pop_back(); // the fixture, which the client does not load
  auto clientFull = ReadLines(argv[4]);
  auto clientLight = ReadLines(argv[5]);
  auto same = [](std::vector<std::string> a, std::vector<std::string> b) {
    std::transform(a.begin(), a.end(), a.begin(), Lower);
    std::transform(b.begin(), b.end(), b.begin(), Lower);
    return a == b;
  };
  Check(same(full, clientFull),
        "the server's full plugins are the client's, in its order (" +
          std::to_string(full.size()) + " here, " +
          std::to_string(clientFull.size()) + " in the client)");
  Check(same(light, clientLight),
        "the server's light plugins are the client's, in its order (" +
          std::to_string(light.size()) + " here, " +
          std::to_string(clientLight.size()) + " in the client)");

  // 2. The beard head parts on WWG Ghost's character, with the ids the game
  // gave them on the main build on 5 Oct.
  auto fileOf = [&](const espm::LookupResult& r) {
    return r.rec ? loader.GetFileNames()[r.fileIdx] : std::string("nothing");
  };
  for (auto [id, edid] :
       { std::pair<uint32_t, const char*>{ 0xFE029827, "KhisartinBeards40" },
         { 0xFE029838, "KhisartinBeardsMedium01_1bit" } }) {
    auto all = br.LookupByIdAll(id);
    bool fromBeards = std::any_of(all.begin(), all.end(), [&](auto& r) {
      return fileOf(r) == "KhisartinBeards.esp" && r.rec->GetType() == "HDPT" &&
        std::string(r.rec->GetEditorId(cache)) == edid;
    });
    Check(fromBeards,
          Hex(id) + " is " + edid + ", the HDPT in KhisartinBeards.esp");
  }

  // 3. Descriptors, the way the gamemode converts with getDescFromId and
  // getIdFromDesc.
  try {
    auto desc = FormDesc::FromFormId(0xFE029827, lo);
    Check(desc.ToString() == "827:KhisartinBeards.esp",
          "0xfe029827 is the descriptor 827:KhisartinBeards.esp (got " +
            desc.ToString() + ")");
  } catch (std::exception& e) {
    Check(false, std::string("0xfe029827 has a descriptor: ") + e.what());
  }
  try {
    auto id = FormDesc::FromString("838:KhisartinBeards.esp").ToFormId(lo);
    Check(id == 0xFE029838,
          "838:KhisartinBeards.esp is 0xfe029838 (got " + Hex(id) + ")");
  } catch (std::exception& e) {
    Check(false, std::string("838:KhisartinBeards.esp has an id: ") + e.what());
  }
  try {
    FormDesc::FromString("1827:KhisartinBeards.esp").ToFormId(lo);
    Check(false,
          "1827:KhisartinBeards.esp is refused: a light plugin's ids have "
          "12 bits");
  } catch (std::exception& e) {
    Check(true,
          std::string("1827:KhisartinBeards.esp is refused: ") + e.what());
  }

  // Every recipe and placed reference the server can see, round trip.
  size_t roundTrips = 0, roundTripFails = 0, lightRecipes = 0;
  std::string firstFail;
  for (auto type : { "COBJ", "REFR" }) {
    for (auto& r : br.GetDistinctRecordsByType(type)) {
      const uint32_t id = r.ToGlobalId(r.rec->GetId());
      if (std::string(type) == "COBJ" && (id >> 24) == 0xFE) {
        ++lightRecipes;
      }
      ++roundTrips;
      try {
        auto back = FormDesc::FromString(FormDesc::FromFormId(id, lo).ToString())
                      .ToFormId(lo);
        if (back != id || br.LookupById(id).rec != r.rec) {
          throw std::runtime_error("came back as " + Hex(back));
        }
      } catch (std::exception& e) {
        if (!roundTripFails++) {
          firstFail = Hex(id) + " from " + fileOf(r) + ": " + e.what();
        }
      }
    }
  }
  Check(roundTripFails == 0,
        "every recipe and placed reference goes id to descriptor to the same "
        "id and record (" +
          std::to_string(roundTrips) + " forms, " +
          std::to_string(roundTripFails) + " failed" +
          (firstFail.empty() ? "" : ", first " + firstFail) + ")");

  // 4. The loom. MoreCraftableEquipment.esp is light (index 14, 0x00E).
  Check(lightRecipes > 0,
        "recipes from light plugins have the game's 0xFE ids (" +
          std::to_string(lightRecipes) + ")");
  auto loomBase = br.LookupById(0xFE00E801);
  Check(loomBase.rec && loomBase.rec->GetType() == "FURN" &&
          std::string(loomBase.rec->GetEditorId(cache)) ==
            "MCECraftingLoomMarker",
        "0xfe00e801 is the loom, MCECraftingLoomMarker in "
        "MoreCraftableEquipment.esp (found " +
          fileOf(loomBase) + ")");
  auto loomRef = br.LookupById(0xFE00E89B);
  Check(loomRef.rec && loomRef.rec->GetType() == "REFR" &&
          loomRef.ToGlobalId(
            espm::Convert<espm::REFR>(loomRef.rec)->GetData(cache).baseId) ==
            0xFE00E801,
        "0xfe00e89b is a placed loom whose base is 0xfe00e801");
  try {
    auto bandageId =
      FormDesc::FromString("154c4:Thornswood-Content.esp").ToFormId(lo);
    auto bandage = br.LookupById(bandageId);
    auto cobj = espm::Convert<espm::COBJ>(bandage.rec);
    auto d = cobj ? cobj->GetData(cache) : espm::COBJ::Data();
    const uint32_t bench = bandage.ToGlobalId(d.benchKeywordId);
    auto loomKeywords = loomBase.rec
      ? loomBase.rec->GetKeywordIds(cache)
      : std::vector<uint32_t>();
    bool loomHasIt = false;
    for (auto k : loomKeywords) {
      loomHasIt = loomHasIt || loomBase.ToGlobalId(k) == bench;
    }
    Check(cobj && std::string(bandage.rec->GetEditorId(cache)) ==
              "ThornswoodRecipeBandage" &&
            bench == 0x016CE000 && loomHasIt,
          "the Bandage recipe asks for keyword 0x16ce000 and the loom "
          "carries it");
  } catch (std::exception& e) {
    Check(false, std::string("the Bandage recipe resolves: ") + e.what());
  }

  // 5. A light plugin changing a light master's placed reference: the
  // Embers XD patch moves table 0x30 of GourmetTables.esp (light index 22,
  // 0x016). Both must land on one id, and chunk loading must find it the
  // way WorldState::GetNeighborsByPosition does.
  {
    const uint32_t tableId = 0xFE016030;
    auto all = br.LookupByIdAll(tableId);
    auto last = br.LookupById(tableId);
    Check(all.size() == 2 && fileOf(all[0]) == "GourmetTables.esp" &&
            fileOf(last) == "Embers XD - Patch - Gourmet Tables.esp",
          "0xfe016030 is in GourmetTables.esp and the Embers XD patch "
          "changes it (" +
            std::to_string(all.size()) + " records, last from " +
            fileOf(last) + ")");
    bool found = false;
    if (last.rec) {
      auto refr = espm::Convert<espm::REFR>(last.rec);
      auto loc = refr->GetData(cache).loc;
      const uint32_t cellOrWorld =
        last.ToGlobalId(espm::GetWorldOrCell(br, last.rec));
      const auto x = static_cast<int16_t>(loc->pos[0] / 4096);
      const auto y = static_cast<int16_t>(loc->pos[1] / 4096);
      for (size_t i = 0; i < loader.GetFileNames().size(); ++i) {
        auto combMapping = br.GetCombMapping(i);
        auto rawMapping = br.GetRawMapping(i);
        uint32_t mapped = espm::utils::GetMappedId(cellOrWorld, *rawMapping);
        auto records = br.GetRecordsAtPos(mapped, x, y);
        for (auto rec : *records[i]) {
          found =
            found || espm::utils::GetMappedId(rec->GetId(), *combMapping) ==
              tableId;
        }
      }
    }
    Check(found, "loading its cell finds 0xfe016030");
  }

  // 6. A full plugin with a light master, both ways.
  {
    auto all = br.LookupByIdAll(0xFE029827);
    auto last = br.LookupById(0xFE029827);
    Check(all.size() == 2 && fileOf(last) == "ThornswoodFullOverLight.esp",
          "a full plugin's change to its light master's 0x827 lands on "
          "0xfe029827 (" +
            std::to_string(all.size()) + " records, last from " +
            fileOf(last) + ")");
    auto list = br.LookupById(0x30000800);
    auto flst = espm::Convert<espm::FLST>(list.rec);
    auto ids = flst ? flst->GetData(cache).formIds : std::vector<uint32_t>();
    Check(ids.size() == 1 && list.ToGlobalId(ids[0]) == 0xFE029827,
          "and the full plugin, the 49th full one (0x30), names it "
          "0xfe029827 in its own form list");
  }

  // 7. A light plugin with a form the game cannot tell from another.
  {
    const auto bad = Fixture::Write(
      scratch, "ThornswoodWideLight.esp",
      Fixture::Tes4(0x200, {}) +
        Fixture::Group(
          "FLST",
          Fixture::Record("FLST", 0, 0x00001800,
                          Fixture::Field("EDID", Fixture::Z("Wide")))));
    std::string error;
    try {
      espm::Loader wide(std::vector<fs::path>{ bad }, nullptr,
                        espm::Loader::BufferType::AllocatedBuffer);
    } catch (std::exception& e) {
      error = e.what();
    }
    Check(error.find("ThornswoodWideLight.esp") != std::string::npos &&
            error.find("0x00001800") != std::string::npos,
          "a light plugin with form 0x1800 is refused by name (" +
            (error.empty() ? std::string("loaded") : error) + ")");
  }

#ifdef THORNSWOOD_LIGHT_PLUGINS
  std::printf("INFO  full plugins %zu, light plugins %zu\n",
              lo.GetNumFullPlugins(), lo.GetNumLightPlugins());
#endif

  std::printf(failures ? "FAILED %d\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
