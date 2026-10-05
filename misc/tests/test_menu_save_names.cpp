// The save SkyrimPlatform builds a menu-picked character on names forms by
// their plugin's place in its own plugin list. The template lists the five
// masters as Skyrim.esm, Update.esm, Dragonborn.esm, Dawnguard.esm,
// HearthFires.esm, and every client loads them as Skyrim.esm, Update.esm,
// Dawnguard.esm, HearthFires.esm, Dragonborn.esm. Measured on 5 Oct 2026:
// its form id array holds 10,562 entries, 4,087 of them Dragonborn.esm's,
// 2,615 Dawnguard.esm's and 3,845 HearthFires.esm's.
//
// When LoadGame lists the client's plugins in the save, every entry the
// template already holds must still name the plugin it named, and a form
// the client names by its own load order must reach the save naming the
// same plugin. The written file is read back with SaveContainerFixture.h,
// which follows Wrye Bash and ReSaver rather than savefile.
//
// The same source builds against savefile before the change, which replaced
// the template's list with the client's (OverwritePluginInfo).
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "SaveContainerFixture.h"
#include <cstdio>
#include <functional>
#include <tuple>
#include <map>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using Names = std::vector<std::string>;

// After the change: the client's lists go in through ListPlugins, and the
// client's form ids through the remap it returns.
template <class S>
auto List(S& save, const Names& full, const Names& light, int)
  -> decltype(save.ListPlugins(full, light).ToSaveFormId(0u), std::function<uint32_t(uint32_t)>())
{
  auto remap = save.ListPlugins(full, light);
  return [remap](uint32_t id) { return remap.ToSaveFormId(id); };
}
// Before: LoadGame put the client's form ids in as they were, then
// replaced the save's list with the client's.
template <class S>
std::function<uint32_t(uint32_t)> List(S& save, const Names& full, const Names&, long)
{
  auto copy = full;
  save.OverwritePluginInfo(copy);
  return [](uint32_t id) { return id; };
}

static std::string Hex(uint32_t v)
{
  char t[11];
  std::snprintf(t, sizeof(t), "0x%08X", v);
  return t;
}

int main(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  namespace F = SaveContainerFixture;
  const auto templateBytes = F::ReadFile(argv[1]);
  const auto templ = F::Parse(templateBytes);
  const auto templIds = F::FormIdArray(templ);

  std::map<std::string, size_t> before;
  for (auto id : templIds) ++before[F::PluginOf(templ, id)];
  std::printf("template lists:");
  for (auto& p : templ.plugins) std::printf(" %s", p.c_str());
  std::printf("\ntemplate form id array: %zu entries\n", templIds.size());

  // The client's lists, in the game's order (thornswood-loadorder.txt of
  // 5 Oct begins with these).
  const Names full = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
                       "Dragonborn.esm", "ccBGSSSE001-Fish.esm", "Skyrim Unbound.esp" };
  const Names light = {};

  auto save = SaveFile_::Reader(std::string(argv[1])).GetStructure();
  CHECK(save != nullptr);
  if (!save) return 1;
  auto toSave = List(*save, full, light, 0);

  // A Dawnguard.esm form as the client numbers it (index 2), and a form of a
  // plugin the template does not list (index 6). Which forms they are does
  // not matter here, only the plugin each names.
  const uint32_t dawnguardForm = 0x02001408, unboundForm = 0x0619bad4;
  const auto dawnguardRef = SaveFile_::RefID::CreateRefId(*save, toSave(dawnguardForm));
  const auto unboundRef = SaveFile_::RefID::CreateRefId(*save, toSave(unboundForm));

  const std::string outPath = std::string(argv[2]) + "/listed.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(outPath));
  const auto out = F::Parse(F::ReadFile(outPath));
  CHECK(F::CheckOffsets(out).empty());
  CHECK(out.pluginInfoRead == out.pluginInfoSize);
  const auto ids = F::FormIdArray(out);

  // Every entry the template held names the plugin it named.
  size_t moved = 0;
  std::map<std::string, size_t> movedTo;
  for (size_t i = 0; i < templIds.size() && i < ids.size(); ++i) {
    const auto was = F::PluginOf(templ, templIds[i]);
    const auto now = F::PluginOf(out, ids[i]);
    if (was != now) {
      ++moved;
      ++movedTo[was + " -> " + (now.empty() ? "(nothing)" : now)];
    }
  }
  for (auto& [what, n] : movedTo) std::printf("     %zu entries %s\n", n, what.c_str());
  std::printf("%s %zu of the template's %zu entries name another plugin after the client's lists go in\n",
              moved ? "FAIL" : "ok  ", moved, templIds.size());
  if (moved) ++failures;

  // The client's forms name the plugins the client meant.
  auto entry = [&](const SaveFile_::RefID& ref) {
    const uint32_t index = (uint32_t(ref.byte0 & 0x3f) << 16) | (uint32_t(ref.byte1) << 8) | ref.byte2;
    return index == 0 || index > ids.size() ? 0xFFFFFFFFu : ids[index - 1];
  };
  for (auto [ref, form, plugin] : { std::tuple{ dawnguardRef, dawnguardForm, "Dawnguard.esm" },
                                    std::tuple{ unboundRef, unboundForm, "Skyrim Unbound.esp" } }) {
    const uint32_t saved = entry(ref);
    const std::string named = saved == 0xFFFFFFFFu ? "" : F::PluginOf(out, saved);
    const bool ok = named == plugin && (saved & 0xFFFFFF) == (form & 0xFFFFFF);
    std::printf("%s client form %s -> save entry %s -> %s (meant %s)\n", ok ? "ok  " : "FAIL",
                Hex(form).c_str(), Hex(saved).c_str(), named.empty() ? "(nothing)" : named.c_str(), plugin);
    if (!ok) ++failures;
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS the menu save keeps every form naming its plugin\n");
  return 0;
}
