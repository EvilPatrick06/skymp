// savefile reads and writes the saves SkyrimPlatform builds characters on:
// the Legendary Edition template byte for byte, and Special Edition saves
// (save version 12: a compression type in the header, an RGBA screenshot, a
// body stored plain, zlib or LZ4, and from form version 78 a light plugin
// list). Every written file is read back with SaveContainerFixture.h, which
// follows Wrye Bash and ReSaver rather than savefile.
//
// The same source builds against savefile before this change; there the
// Legendary Edition round trip runs and the Special Edition part reports
// that savefile cannot read those saves.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "SaveContainerFixture.h"
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace F = SaveContainerFixture;
using Names = std::vector<std::string>;
using Bytes = std::vector<uint8_t>;

template <class S>
constexpr auto HasLightPlugins(int) -> decltype(std::declval<S&>().lightPluginInfo, bool()) { return true; }
template <class S>
constexpr bool HasLightPlugins(long) { return false; }

static bool Throws(const std::function<void()>& f)
{
  try { f(); } catch (std::exception&) { return true; }
  return false;
}

static Bytes WriteAndLoad(const std::shared_ptr<SaveFile_::SaveFile>& save, const std::string& path)
{
  CHECK(SaveFile_::Writer(save).CreateSaveFile(path));
  return F::ReadFile(path);
}

// A file's bytes with its body decompressed and its compression type
// cleared, so files that differ only in how the body is stored compare equal.
static Bytes Flat(const Bytes& file)
{
  auto c = F::Parse(file);
  if (c.version >= 12) {
    const size_t at = c.headerEnd - size_t(c.shotWidth) * c.shotHeight * 4 - 2;
    c.flat[at] = c.flat[at + 1] = 0;
  }
  return c.flat;
}

// LZ4 block from liblz4 1.9.4 (python-lz4 4.4.5, lz4.block.compress with
// store_size=False) of the input Lz4Input() builds: literal and match lengths
// past 15, an overlapping match and a long run.
static const char* kLz4Block =
#include "Lz4ReferenceBlock.inc"
  ;

static Bytes Lz4Input()
{
  Bytes d;
  const std::string magic = "TESV_SAVEGAME";
  d.insert(d.end(), magic.begin(), magic.end());
  for (int r = 0; r < 3; ++r)
    for (int i = 0; i < 256; ++i) d.push_back(uint8_t(i));
  d.insert(d.end(), 300, 'A');
  const std::string names = "Skyrim.esm Update.esm Dawnguard.esm ";
  for (int r = 0; r < 20; ++r) d.insert(d.end(), names.begin(), names.end());
  uint32_t x = 1;
  for (int i = 0; i < 600; ++i) {
    x = x * 1103515245u + 12345u;
    d.push_back(uint8_t(x >> 24));
  }
  d.insert(d.end(), 1000, 0);
  return d;
}

// S is savefile's SaveFile and R its Reader, as template parameters so that
// this also builds against savefile before the change.
template <class S, class R>
void SpecialEdition(const Bytes& templateBytes, const std::string& work)
{
  if constexpr (!HasLightPlugins<S>(0)) {
    ++failures;
    std::printf("FAIL savefile has no Special Edition support (no compression type, RGBA screenshot or light plugin list)\n");
  } else {
    // The liblz4 vector decodes.
    Bytes block;
    for (const char* p = kLz4Block; *p; p += 2) block.push_back(uint8_t(std::stoi(std::string(p, 2), nullptr, 16)));
    const Bytes expected = Lz4Input();
    CHECK(R::Lz4BlockDecompress(block.data(), block.size(), expected.size()) == expected);
    CHECK(Throws([&] { R::Lz4BlockDecompress(block.data(), block.size() - 1, expected.size()); }));
    CHECK(Throws([&] { R::Lz4BlockDecompress(block.data(), block.size(), expected.size() + 1); }));

    const auto templ = F::Parse(templateBytes);
    const Names light = { "ccQDRSSE001-SurvivalMode.esl", "KhisartinBeards.esp" };
    Bytes plain;
    for (uint16_t compression : { 0, 1, 2 }) {
      // A Special Edition save to read: the template if it is one, else the
      // template's body in a Special Edition container.
      Bytes input;
      if (templ.version >= 12 && compression == templ.compression) {
        input = templateBytes;
      } else if (templ.version >= 12) {
        // The game's own save stored the other way. savefile writes no LZ4,
        // so an LZ4 one is only read; the fixture reader checks what
        // savefile wrote before it is used.
        if (compression == S::COMPRESSION_LZ4) continue;
        const std::string from = work + "/se-template.ess";
        F::WriteFile(from, templateBytes);
        std::shared_ptr<S> other = R(from).GetStructure();
        other->header.compressionType = compression;
        input = WriteAndLoad(other, work + "/se-" + std::to_string(compression) + "-made.ess");
        CHECK(Flat(input) == Flat(templateBytes));
      } else {
        input = F::SpecialEditionFixture(templateBytes, light, compression);
      }
      const std::string tag = "compression " + std::to_string(compression);
      const std::string inPath = work + "/se-" + std::to_string(compression) + ".ess";
      F::WriteFile(inPath, input);
      const auto in = F::Parse(input);
      CHECK(F::CheckOffsets(in).empty());

      std::shared_ptr<S> save = R(inPath).GetStructure();
      CHECK(save->IsSpecialEdition());
      CHECK(save->header.compressionType == compression);
      CHECK(save->screenshotData.size() == size_t(in.shotWidth) * in.shotHeight * 4);
      CHECK(save->formVersion == in.formVersion);
      CHECK(save->HasLightPluginInfo() == in.hasLightList);
      CHECK(save->pluginInfo.pluginsName == in.plugins);
      CHECK(save->lightPluginInfo.pluginsName == in.lightPlugins);
      CHECK(save->pluginInfoSize == in.pluginInfoSize);
      CHECK(save->CalculatePluginInfoSize() == in.pluginInfoSize);
      CHECK(save->formIDArray == F::FormIdArray(in));

      // savefile writes no LZ4; such a save is written with its body plain.
      if (compression == S::COMPRESSION_LZ4)
        save->header.compressionType = S::COMPRESSION_NONE;
      const auto written = WriteAndLoad(save, work + "/se-" + std::to_string(compression) + "-out.ess");
      const auto out = F::Parse(written);
      CHECK(F::CheckOffsets(out).empty());
      CHECK(out.pluginInfoRead == out.pluginInfoSize);
      if (compression != S::COMPRESSION_LZ4) {
        CHECK(written == input); // byte for byte, compressed body included
      }
      CHECK(Flat(written) == Flat(input));
      if (plain.empty()) plain = Flat(input);
      CHECK(Flat(written) == plain); // every compression holds the same save

      // What savefile wrote, it reads back to the same bytes.
      std::shared_ptr<S> again = R(work + "/se-" + std::to_string(compression) + "-out.ess").GetStructure();
      CHECK(WriteAndLoad(again, work + "/se-" + std::to_string(compression) + "-again.ess") == written);
      std::printf("%s Special Edition %s: read, written and read back\n", failures ? "...." : "ok  ", tag.c_str());
    }

    // Listing the game's plugins: the save's lists keep their order and grow
    // by the plugins they do not hold, and the game's ids become the save's.
    const std::string inPath = work + "/se-lists.ess";
    F::WriteFile(inPath, templ.version >= 12 ? templateBytes : F::SpecialEditionFixture(templateBytes, light, 0));
    std::shared_ptr<S> save = R(inPath).GetStructure();
    const Names savePlugins = save->pluginInfo.pluginsName;
    const Names saveLights = save->lightPluginInfo.pluginsName;
    auto findIn = [](const Names& list, std::string name) {
      auto lower = [](std::string v) {
        for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
        return v;
      };
      for (size_t i = 0; i < list.size(); ++i)
        if (lower(list[i]) == lower(name)) return int(i);
      return -1;
    };
    // The game's lists: the five masters in the engine's order, a plugin
    // the save does not list, and KhisartinBeards.esp at light index 0x29,
    // named in other case (the game ignores case).
    const Names gamePlugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
                                "Dragonborn.esm", "RaceMenu.esp" };
    Names gameLights;
    while (gameLights.size() < 0x29) gameLights.push_back("Light" + std::to_string(gameLights.size()) + ".esl");
    gameLights.push_back("khisartinbeards.esp");
    gameLights.push_back("HammerHair.esp");
    Names expectPlugins = savePlugins, expectLights = saveLights;
    for (auto& p : gamePlugins) if (findIn(expectPlugins, p) < 0) expectPlugins.push_back(p);
    for (auto& p : gameLights) if (findIn(expectLights, p) < 0) expectLights.push_back(p);

    const auto remap = save->ListPlugins(gamePlugins, gameLights);
    CHECK(save->pluginInfo.pluginsName == expectPlugins);
    CHECK(save->lightPluginInfo.pluginsName == expectLights);
    CHECK(save->pluginInfoSize == save->CalculatePluginInfoSize());
    const int beardAt = findIn(expectLights, "KhisartinBeards.esp");
    const uint32_t beard = 0xFE000000u | (uint32_t(beardAt) << 12) | 0x827;
    CHECK(remap.ToSaveFormId(0xFE029827u) == beard);
    CHECK(remap.ToSaveFormId(0x02001408u) == ((uint32_t(findIn(expectPlugins, "Dawnguard.esm")) << 24) | 0x1408));
    CHECK(remap.ToSaveFormId(0x04000800u) == ((uint32_t(findIn(expectPlugins, "Dragonborn.esm")) << 24) | 0x800));
    CHECK(remap.ToSaveFormId(0x05000800u) == ((uint32_t(findIn(expectPlugins, "RaceMenu.esp")) << 24) | 0x800));
    CHECK(remap.ToSaveFormId(0x00013746u) == 0x00013746u);
    CHECK(remap.ToSaveFormId(0xFF000123u) == 0xFF000123u);
    CHECK(Throws([&] { remap.ToSaveFormId(0x06000001u); })); // the game listed six
    CHECK(Throws([&] { remap.ToSaveFormId(0xFE02B001u); })); // and 0x2B light plugins

    const auto ref = SaveFile_::RefID::CreateRefId(*save, remap.ToSaveFormId(0xFE029827u));
    CHECK(!Throws([&] { save->CheckFormIdArrayPlugins(); }));
    // savefile writes no LZ4; LoadGame::Run writes such a body with zlib.
    if (save->header.compressionType == S::COMPRESSION_LZ4)
      save->header.compressionType = S::COMPRESSION_ZLIB;
    const auto listed = F::Parse(WriteAndLoad(save, work + "/se-lists-out.ess"));
    CHECK(F::CheckOffsets(listed).empty());
    CHECK(listed.pluginInfoRead == listed.pluginInfoSize);
    CHECK(listed.plugins == expectPlugins);
    CHECK(listed.lightPlugins == expectLights);
    const auto ids = F::FormIdArray(listed);
    const uint32_t index = (uint32_t(ref.byte0 & 0x3f) << 16) | (uint32_t(ref.byte1) << 8) | ref.byte2;
    CHECK(index >= 1 && index <= ids.size() && ids[index - 1] == beard);
    CHECK(findIn({ F::PluginOf(listed, beard) }, "KhisartinBeards.esp") == 0); // any case

    // Listing the same plugins again changes nothing.
    const auto sizeBefore = save->pluginInfoSize;
    save->ListPlugins(gamePlugins, gameLights);
    CHECK(save->pluginInfoSize == sizeBefore);
    CHECK(save->pluginInfo.pluginsName == expectPlugins);
    std::printf("%s plugin lists grow and keep what the save names\n", failures ? "...." : "ok  ");
  }
}

// A Legendary Edition save has no light plugin list to name a light form by.
template <class S>
void LegendaryEdition(const std::shared_ptr<S>& save)
{
  if constexpr (HasLightPlugins<S>(0)) {
    CHECK(!save->HasLightPluginInfo());
    CHECK(Throws([&] { SaveFile_::RefID::CreateRefId(*save, 0xFE029827u); }));
    const auto remap = save->ListPlugins(save->pluginInfo.pluginsName, { "KhisartinBeards.esp" });
    CHECK(save->lightPluginInfo.pluginsName.empty());
    CHECK(Throws([&] { remap.ToSaveFormId(0xFE000827u); }));
  }
}

static int Run(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  const std::string templatePath = argv[1], work = argv[2];
  const Bytes templateBytes = F::ReadFile(templatePath);
  const auto templ = F::Parse(templateBytes);
  std::printf("template: save version %u, form version %u, compression %u, %zu plugins, %zu light plugins\n",
              templ.version, templ.formVersion, templ.compression, templ.plugins.size(), templ.lightPlugins.size());

  // The template, read and written back, is the same file.
  auto save = SaveFile_::Reader(templatePath).GetStructure();
  if (templ.compression != 2) {
    CHECK(WriteAndLoad(save, work + "/template-out.ess") == templateBytes);
  }
  CHECK(F::CheckOffsets(templ).empty());
  std::printf("%s template round trip\n", failures ? "...." : "ok  ");

  if (templ.version < 12) LegendaryEdition(save);

  SpecialEdition<SaveFile_::SaveFile, SaveFile_::Reader>(templateBytes, work);

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS savefile reads and writes Legendary and Special Edition saves and lists light plugins\n");
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
