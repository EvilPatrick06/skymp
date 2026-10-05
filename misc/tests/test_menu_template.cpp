// Checks the save SkyrimPlatform builds every menu-picked character on
// (skyrim_platform/assets/template.ess) against what LoadGame needs from it.
//
// It must be a Special Edition save at form version 78, because only those
// carry the light plugin list a light plugin's form is named through (Wrye
// Bash save_headers.py, ReSaver ESS.supportsESL). A Legendary Edition save
// cannot simply be relabelled: its Papyrus block indexes strings with 16 bits
// and a Special Edition one with 32 (ReSaver StringTable, ESS.isStr32), so
// the game itself has to write the template.
//
// LoadGame keeps the template's plugin lists and adds the client's other
// plugins after them (SaveFile::ListPlugins), and the game finds each plugin
// a save lists by its file name. So the template may list its plugins in any
// order, but only ones every client loads: the ten Steam installs with the
// game, which thornswood-front.js and the launcher's Verify.cs also name.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "SaveContainerFixture.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static std::string Lower(std::string s)
{
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

int main(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  namespace F = SaveContainerFixture;
  const auto bytes = F::ReadFile(argv[1]);
  const auto t = F::Parse(bytes);
  std::printf("template: save version %u, form version %u, compression %u\n", t.version, t.formVersion, t.compression);
  std::printf("regular plugins:");
  for (auto& p : t.plugins) std::printf(" %s", p.c_str());
  std::printf("\nlight plugins: %zu\n", t.lightPlugins.size());

  CHECK(t.version >= 12);
  CHECK(t.formVersion >= 78);
  CHECK(t.hasLightList);
  const std::vector<std::string> steam = { "skyrim.esm", "update.esm", "dawnguard.esm",
                                           "hearthfires.esm", "dragonborn.esm",
                                           "ccbgssse001-fish.esm", "ccbgssse025-advdsgs.esm",
                                           "ccbgssse037-curios.esl", "ccqdrsse001-survivalmode.esl",
                                           "_resourcepack.esl" };
  auto lists = t.plugins;
  lists.insert(lists.end(), t.lightPlugins.begin(), t.lightPlugins.end());
  for (auto& p : lists) {
    if (std::find(steam.begin(), steam.end(), Lower(p)) == steam.end()) {
      ++failures;
      std::printf("FAIL the template lists %s, which not every client loads\n", p.c_str());
    }
  }
  CHECK(F::CheckOffsets(t).empty());
  CHECK(t.pluginInfoRead == t.pluginInfoSize);

  auto save = SaveFile_::Reader(std::string(argv[1])).GetStructure();
  CHECK(save != nullptr);
  if (!save) return 1;
  bool named = true;
  try { save->CheckFormIdArrayPlugins(); } catch (std::exception& e) { named = false; std::printf("FAIL %s\n", e.what()); }
  CHECK(named);

  // What LoadGame::Run edits.
  auto playerBase = save->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::PlayerBase), uint8_t(SaveFile_::ChangeForm::Type::NPC));
  CHECK(playerBase != nullptr); // ModifyPlayerFormNPC
  auto player = std::find_if(save->changeForms.begin(), save->changeForms.end(),
                             [](auto& form) { return form.formID.IsPlayerID(); });
  CHECK(player != save->changeForms.end()); // ModifyEssStructure
  if (player != save->changeForms.end()) {
    CHECK(player->length2 > 0); // ModifyEssStructure requires it compressed
    if (player->length2 > 0) {
      std::vector<uint8_t> data(player->length2);
      uLongf len = player->length2;
      CHECK(uncompress(data.data(), &len, player->data.data(), player->length1) == Z_OK && len == player->length2);
    }
  }
  CHECK(save->globalDataTable1.size() > SaveFile_::SaveFile::WEATHER_INDEX);
  if (save->globalDataTable1.size() > SaveFile_::SaveFile::WEATHER_INDEX) {
    CHECK(save->globalDataTable1[SaveFile_::SaveFile::GLOBAL_VARIABLES_INDEX].type == SaveFile_::SaveFile::GLOBAL_VARIABLES_INDEX);
    CHECK(save->globalDataTable1[SaveFile_::SaveFile::WEATHER_INDEX].type == SaveFile_::SaveFile::WEATHER_INDEX);
    SaveFile_::RefID gameHour = 0x38; // ModifySaveTime: GameHour, Skyrim.esm
    auto index = save->FindIndexInFormIdArray(0x38);
    if (index >= 0) gameHour = SaveFile_::RefID(static_cast<uint32_t>(index));
    CHECK(save->GetGlobalvariableByRefID(gameHour) != nullptr);
  }
  CHECK(std::any_of(save->globalDataTable1.begin(), save->globalDataTable1.end(),
                    [](auto& g) { return g.type == SaveFile_::PlayerLocation::GlobalDataType; }));

  // savefile writes it back unchanged.
  if (save->header.compressionType == SaveFile_::SaveFile::COMPRESSION_LZ4)
    save->header.compressionType = SaveFile_::SaveFile::COMPRESSION_NONE;
  const std::string out = std::string(argv[2]) + "/template-written.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(out));
  auto written = F::Parse(F::ReadFile(out));
  auto original = t;
  if (written.version >= 12) {
    const size_t at = written.headerEnd - size_t(written.shotWidth) * written.shotHeight * 4 - 2;
    written.flat[at] = written.flat[at + 1] = original.flat[at] = original.flat[at + 1] = 0;
  }
  CHECK(written.flat == original.flat);

  if (failures) { std::printf("%d check(s) failed: this template cannot carry light plugin forms\n", failures); return 1; }
  std::printf("PASS the template is a form version 78 Special Edition save LoadGame can build on\n");
  return 0;
}
