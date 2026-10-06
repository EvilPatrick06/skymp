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
//
// LoadGame edits the player's change form (ModifyEssStructure: position,
// cell, initial inventory). The game stores a change form zlib-compressed or
// as it is (ReSaver ChangeForm.java: compressed when length2 > 0): the
// Legendary Edition template holds the player's compressed, a new game
// SkyrimSE 1.6.1170 saved on 5 Oct 2026 holds it plain. So the template's
// player form is edited both ways, by LoadGame's own functions, which
// test_light_plugins_save.ps1 copies out of LoadGame.cpp unchanged.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "savefile/SFSeekerOfDifferences.h"
#include "SaveContainerFixture.h"
#include "InitialInventory.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

class LoadGame
{
public:
  static SaveFile_::PlayerLocation* FindSectionWithPlayerLocation(std::shared_ptr<SaveFile_::SaveFile> save);
  static SaveFile_::PlayerLocation CreatePlayerLocation(const std::array<float, 3>& pos, const SaveFile_::RefID& world);
  static std::vector<uint8_t> Decompress(const SaveFile_::ChangeForm& changeForm);
  static std::vector<uint8_t> ReadChangeFormData(const SaveFile_::ChangeForm& changeForm);
  static void RewriteChangeFormData(std::shared_ptr<SaveFile_::SaveFile> save, SaveFile_::ChangeForm& changeForm,
                                    const std::vector<uint8_t>& data);
  static void EditChangeForm(std::vector<uint8_t>& data, const std::array<float, 3>& pos,
                             const std::array<float, 3>& angle, const SaveFile_::RefID& world);
  static std::vector<uint8_t> Compress(const std::vector<uint8_t>& uncompressed);
  static void WriteChangeForm(std::shared_ptr<SaveFile_::SaveFile> save, SaveFile_::ChangeForm& changeForm,
                              const std::vector<uint8_t>& compressed, size_t uncompressedSize);
  static void ModifyEssStructure(std::shared_ptr<SaveFile_::SaveFile> save, std::array<float, 3> pos,
                                 std::array<float, 3> angle, uint32_t cellOrWorld,
                                 const std::vector<InitialInventory::Item>* inventory);
};
#include "PlayerFormFunctionsUnderTest.inc"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static std::string Lower(std::string s)
{
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static SaveFile_::ChangeForm* Player(SaveFile_::SaveFile& save)
{
  auto it = std::find_if(save.changeForms.begin(), save.changeForms.end(),
                         [](auto& form) { return form.formID.IsPlayerID(); });
  return it == save.changeForms.end() ? nullptr : &*it;
}

// The player form's data, read with zlib here rather than through LoadGame.
static std::vector<uint8_t> PlainData(const SaveFile_::ChangeForm& form)
{
  if (form.length2 == 0) return form.data;
  std::vector<uint8_t> data(form.length2);
  uLongf len = form.length2;
  if (uncompress(data.data(), &len, form.data.data(), form.length1) != Z_OK || len != form.length2) return {};
  return data;
}

// LoadGame::ModifyEssStructure on the template, its player form stored
// compressed or plain, and what comes out read back by the fixture reader
// and by savefile.
// With an initial inventory, as a new character is created (remoteServer.ts
// passes one at creation): InitialInventory::Replace rewrites the inventory
// section of the template's player form.
static void EditsPlayer(const std::string& templatePath, const std::string& work, bool compressed,
                        bool withInventory)
{
  namespace F = SaveContainerFixture;
  const char* how = compressed ? "compressed" : "plain";
  const std::string tag = std::string(how) + (withInventory ? "-inventory" : "");
  auto save = SaveFile_::Reader(templatePath).GetStructure();
  auto player = save ? Player(*save) : nullptr;
  CHECK(player != nullptr);
  if (!player) return;
  const auto before = PlainData(*player);
  CHECK(before.size() >= 27);
  if (before.size() < 27) return;

  // The fixture: the same data stored the other way when the template does
  // not store it this way. Checked with the fixture reader before use.
  if ((player->length2 > 0) != compressed) {
    std::vector<uint8_t> packed(compressBound(uLong(before.size())));
    uLongf len = uLongf(packed.size());
    CHECK(compress2(packed.data(), &len, before.data(), uLong(before.size()), Z_DEFAULT_COMPRESSION) == Z_OK);
    packed.resize(len);
    LoadGame::WriteChangeForm(save, *player, compressed ? packed : before, compressed ? before.size() : 0);
  }
  if (save->header.compressionType == SaveFile_::SaveFile::COMPRESSION_LZ4)
    save->header.compressionType = SaveFile_::SaveFile::COMPRESSION_NONE;
  const std::string fixturePath = work + "/player-" + tag + "-in.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(fixturePath));
  CHECK(F::CheckOffsets(F::Parse(F::ReadFile(fixturePath))).empty());
  save = SaveFile_::Reader(fixturePath).GetStructure();
  player = Player(*save);
  CHECK((player->length2 > 0) == compressed);

  const std::array<float, 3> pos = { 1234.5f, -678.25f, 90.f }, angle = { 0.f, 0.f, 90.f };
  bool edited = true;
  try {
    // One gold coin (Gold001, Skyrim.esm 0xF) taken off the base container's.
    const std::vector<InitialInventory::Item> items = { { { 0x40, 0x00, 0x0f }, 1 } };
    LoadGame::ModifyEssStructure(save, pos, angle, 0x3c, withInventory ? &items : nullptr);
  } catch (std::exception& e) {
    edited = false;
    std::printf("FAIL ModifyEssStructure on a %s player form%s threw: %s\n", how,
                withInventory ? " with an initial inventory" : "", e.what());
    ++failures;
  }
  if (!edited) return;

  const std::string outPath = work + "/player-" + tag + "-out.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(outPath));
  CHECK(F::CheckOffsets(F::Parse(F::ReadFile(outPath))).empty());
  auto reread = SaveFile_::Reader(outPath).GetStructure();
  auto out = reread ? Player(*reread) : nullptr;
  CHECK(out != nullptr);
  if (!out) return;
  CHECK((out->length2 > 0) == compressed); // stored the way it was
  const auto after = PlainData(*out);
  CHECK(withInventory || after.size() == before.size());
  if (after.size() < 27) return;
  float written[3];
  std::memcpy(written, after.data() + 3, sizeof(written));
  CHECK(written[0] == pos[0] && written[1] == pos[1] && written[2] == pos[2]);
  if (withInventory) {
    InitialInventory::Cursor read(after);
    read.Skip(35);
    read.Extras();
    CHECK(read.Count() == 1); // the one stack given
  }
  if (!withInventory)
    CHECK(after.size() == before.size() && std::equal(before.begin() + 27, before.end(), after.begin() + 27)); // the rest unchanged
  std::printf("ok   LoadGame edits a %s player change form%s and writes it back %s\n", how,
              withInventory ? ", initial inventory included," : "", how);
}

static int Run(int argc, char** argv)
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
    std::printf("player change form stored %s\n", player->length2 > 0 ? "compressed" : "plain");
    CHECK(PlainData(*player).size() >= 27);
  }
  for (bool withInventory : { false, true }) {
    EditsPlayer(argv[1], argv[2], true, withInventory);
    EditsPlayer(argv[1], argv[2], false, withInventory);
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

// An exception is a failure that says what it was, not a crash.
int main(int argc, char** argv)
{
  try {
    return Run(argc, argv);
  } catch (std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    return 1;
  }
}
