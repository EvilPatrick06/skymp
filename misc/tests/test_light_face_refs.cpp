// A character picked at the main menu is loaded from a save SkyrimPlatform
// builds on its template. That save must name every form the character's
// face uses. The case is Aemon Stark as Thornswood's live server stored him
// on 5 Oct 2026: NordRace 0x13746, seven Skyrim.esm head parts, a beard from
// KhisartinBeards.esp, a light plugin the main build loads at light index
// 0x29 (0xFE029827 and 0xFE029838), and face texture set 0x3B521. Picking him
// crashed the game every time; with Skyrim.esm parts only he loaded.
//
// The beard must reach the save as what it is: a form id array entry
// 0xFE029827 whose light index 0x29 names KhisartinBeards.esp in the save's
// light plugin list. The written file is read back with
// SaveContainerFixture.h, a reader that follows Wrye Bash and ReSaver rather
// than savefile.
//
// The same source builds against savefile before this change, which read
// and wrote only Legendary Edition saves and listed only regular plugins.
// There it builds the save the old code built, and the checks show the beard
// naming nothing.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "LightFaceRefsFixture.h"
#include "SaveContainerFixture.h"
#include "LightFaceApiUnderTest.inc"
class LoadGame { public:
 static void FillChangeForm(std::shared_ptr<SaveFile_::SaveFile>,SaveFile_::ChangeForm*,std::pair<uint32_t,std::vector<uint8_t>>&);
 static void WriteChangeForm(std::shared_ptr<SaveFile_::SaveFile>,SaveFile_::ChangeForm&,const std::vector<uint8_t>&,size_t);
};
#include "LightFaceSaveFunctionsUnderTest.inc"
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using Names = std::vector<std::string>;

template <class S>
constexpr auto HasLightPlugins(int) -> decltype(std::declval<S&>().lightPluginInfo, bool()) { return true; }
template <class S>
constexpr bool HasLightPlugins(long) { return false; }
constexpr bool kLightPlugins = HasLightPlugins<SaveFile_::SaveFile>(0);

// How LoadGameApi builds the face. After the change the client's plugins go
// into the save first and every form id through what that returns; before
// it the face was built from the client's ids as they were and the save's
// plugin list replaced by the client's afterwards.
template <class S>
auto BuildFace(std::shared_ptr<S> save, const Names& plugins, const Names& light,
               Napi::Object npc, int)
  -> decltype(save->ListPlugins(plugins, light), std::unique_ptr<SaveFile_::ChangeFormNPC_>())
{
  const auto remap = save->ListPlugins(plugins, light);
  return CreateChangeFormNpc(save, remap, npc);
}
template <class S>
std::unique_ptr<SaveFile_::ChangeFormNPC_> BuildFace(std::shared_ptr<S> save, const Names& plugins,
                                                      const Names&, Napi::Object npc, long)
{
  auto face = CreateChangeFormNpc(save, npc);
  auto copy = plugins; // the old signature took a mutable list of regular plugins only
  save->OverwritePluginInfo(copy);
  return face;
}

static bool Listed(const Names& list, std::string name)
{
  auto lower = [](std::string v) {
    for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
    return v;
  };
  for (auto& p : list)
    if (lower(p) == lower(name)) return true;
  return false;
}

static std::string Hex(uint32_t v)
{
  char t[11];
  std::snprintf(t, sizeof(t), "0x%08X", v);
  return t;
}

// The form a type 0 RefID names: an entry of the form id array, counted from 1.
static uint32_t Entry(const std::vector<uint32_t>& ids, const SaveFile_::RefID& ref)
{
  if ((ref.byte0 >> 6) != 0) return 0xFFFFFFFF;
  const uint32_t index = (uint32_t(ref.byte0 & 0x3f) << 16) | (uint32_t(ref.byte1) << 8) | ref.byte2;
  return index == 0 || index > ids.size() ? 0xFFFFFFFF : ids[index - 1];
}

int main(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  using nlohmann::json;
  namespace F = SaveContainerFixture;
  const std::string templatePath = argv[1], work = argv[2];

  const auto templateBytes = F::ReadFile(templatePath);
  const auto templ = F::Parse(templateBytes);

  // The client's lists in the game's order: the five masters as the engine
  // loads them (the template lists them in another order), then others.
  const Names plugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
                          "Dragonborn.esm", "ccBGSSSE001-Fish.esm", "RaceMenu.esp" };
  Names light;
  while (light.size() < 0x29) light.push_back("Light" + std::to_string(light.size()) + ".esl");
  light.push_back("KhisartinBeards.esp"); // 0x29 on the main build
  light.push_back("HammerHair.esp");

  // savefile with light plugin support works on a Special Edition save. If
  // the template is still the Legendary Edition one, the test wraps its body
  // in a Special Edition container (SaveContainerFixture.h says why the game
  // itself could not load that one).
  std::string input = templatePath;
  if (kLightPlugins && templ.version < 12) {
    input = work + "/special-edition-fixture.ess";
    F::WriteFile(input, F::SpecialEditionFixture(templateBytes, {}, 0));
  }
  std::printf("savefile %s light plugins; input %s (save version %u)\n",
              kLightPlugins ? "lists" : "does not list", input.c_str(),
              F::Parse(F::ReadFile(input)).version);

  auto save = SaveFile_::Reader(input).GetStructure();
  CHECK(save != nullptr);
  if (!save) return 1;

  const std::vector<uint32_t> regularParts = { 0x5162f, 0x51631, 0x8555f, 0x24244,
                                               0x51508, 0x51457, 0x51491 };
  const std::vector<uint32_t> beard = { 0xfe029827u, 0xfe029838u };
  auto storedParts = json::array();
  for (auto id : regularParts) storedParts.push_back(id);
  for (auto id : beard) storedParts.push_back(id);
  // Race, head parts and face texture set are the stored record's; the skin
  // color and presets are filler.
  json aemon = { { "name", "Aemon Stark" }, { "raceId", 0x13746 }, { "isFemale", false },
                 { "face", { { "bodySkinColor", 0xd9b29b }, { "headPartIds", storedParts },
                             { "headTextureSetId", 0x3b521 },
                             { "presets", json::array({ 1, 0, 2, 0 }) } } } };

  std::unique_ptr<SaveFile_::ChangeFormNPC_> npc;
  try {
    npc = BuildFace(save, plugins, light, Napi::Object(aemon), 0);
  } catch (std::exception& e) {
    std::printf("FAIL building the face threw: %s\n", e.what());
    return 1;
  }
  CHECK(npc->race.has_value() && npc->face.has_value());
  if (!npc->race || !npc->face) return 1;
  CHECK(npc->face->headParts.size() == regularParts.size() + beard.size());

  auto playerBase = save->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::PlayerBase),
                                               uint8_t(SaveFile_::ChangeForm::Type::NPC));
  CHECK(playerBase != nullptr);
  if (!playerBase) return 1;
  auto record = npc->ToBinary();
  LoadGame::FillChangeForm(save, playerBase, record);
  const std::string outPath = work + "/aemon-stark.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(outPath));

  // What the written file names, read without savefile.
  const auto out = F::Parse(F::ReadFile(outPath));
  CHECK(F::CheckOffsets(out).empty());
  CHECK(out.pluginInfoRead == out.pluginInfoSize);
  size_t unlisted = 0, unlistedLight = 0;
  for (auto& p : plugins) unlisted += !Listed(out.plugins, p);
  for (auto& p : light) unlistedLight += !Listed(out.lightPlugins, p);
  CHECK(unlisted == 0);
  CHECK(out.hasLightList);
  CHECK(unlistedLight == 0);
  const auto ids = F::FormIdArray(out);

  // The written record is the face built above, so its RefIDs are these.
  auto reread = SaveFile_::Reader(outPath).GetStructure();
  auto restored = reread ? reread->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::PlayerBase),
                                                        uint8_t(SaveFile_::ChangeForm::Type::NPC))
                         : nullptr;
  CHECK(restored && restored->data == record.second && restored->changeFlags == record.first);

  // Each RefID names the plugin the client's id named, and the same form
  // in it: the id under the plugin is kept, 24 bits for a regular plugin,
  // 12 for a light one.
  auto names = [&](const SaveFile_::RefID& ref, uint32_t form, const std::string& plugin) {
    const uint32_t entry = Entry(ids, ref);
    const std::string named = entry == 0xFFFFFFFF ? "" : F::PluginOf(out, entry);
    const uint32_t local = (form >> 24) == 0xFE ? 0xFFF : 0xFFFFFF;
    const bool ok = entry != 0xFFFFFFFF && (entry & local) == (form & local) && named == plugin;
    std::printf("%s %s -> form id array %s -> %s\n", ok ? "ok  " : "FAIL", Hex(form).c_str(),
                Hex(entry).c_str(), named.empty() ? "(names nothing)" : named.c_str());
    if (!ok) ++failures;
  };
  names(npc->race->myRaceNow, 0x13746, "Skyrim.esm");
  names(npc->race->defaultRace, 0x13746, "Skyrim.esm");
  for (size_t i = 0; i < regularParts.size(); ++i)
    names(npc->face->headParts[i], regularParts[i], "Skyrim.esm");
  for (size_t i = 0; i < beard.size(); ++i)
    names(npc->face->headParts[regularParts.size() + i], beard[i], "KhisartinBeards.esp");
  names(npc->face->headTextureSet, 0x3b521, "Skyrim.esm");

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS the menu save names Aemon Stark's beard through its light plugin list\n");
  return 0;
}
