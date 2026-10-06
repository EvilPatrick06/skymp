#include "libespm/Combiner.h"
#include "libespm/Browser.h"
#include "libespm/Convert.h"
#include "libespm/Records.h"
#include "libespm/Utils.h"
#include "libespm/espm.h"
#include <array>
#include <fmt/format.h>
#include <string>

namespace espm {

Combiner::Combiner()
  : pImpl(nullptr)
{
  pImpl = std::make_unique<CombineBrowser::Impl>();
}

void espm::Combiner::AddSource(Browser* src, const char* fileName)
{
  pImpl->sources.push_back({ src, fileName });
}

std::unique_ptr<espm::CombineBrowser> Combiner::Combine()
{
  // Thornswood #1715. Every plugin gets the slot the game gives it before any
  // id is mapped, because a master can be of either kind: a full plugin can
  // have a light master, and a light plugin full or light ones. The game
  // counts full and light plugins separately, each in load order, and so does
  // this. Before, every plugin took the next top byte, so a light plugin
  // listed here would have taken a top byte the game gives the next full
  // plugin, and every id after it would have been off by one.
  std::vector<PluginSlot> slots;
  slots.reserve(pImpl->sources.size());
  uint32_t numFull = 0;
  uint32_t numLight = 0;
  for (size_t i = 0; i < pImpl->sources.size(); ++i) {
    auto& src = pImpl->sources[i];
    if (!src.br) {
      throw CombineError("nullptr source with index " + std::to_string(i));
    }
    const auto tes4 = Convert<TES4>(src.br->LookupById(0));
    if (!tes4) {
      throw CombineError(src.fileName + " doesn't have TES4 record");
    }
    // GetFlags is the record header's flags field as the file stores it, and
    // for TES4 that is where the game reads the ESL flag from.
    if (IsLightPlugin(src.fileName, tes4->GetFlags())) {
      if (numLight >= PluginSlot::kMaxLightPlugins) {
        throw CombineError(fmt::format(
          "{} is light plugin number {} and the game has room for {}",
          src.fileName, numLight + 1, PluginSlot::kMaxLightPlugins));
      }
      slots.push_back({ true, static_cast<uint16_t>(numLight++) });
    } else {
      if (numFull >= PluginSlot::kMaxFullPlugins) {
        throw CombineError(fmt::format(
          "{} is full plugin number {} and the game has room for {}",
          src.fileName, numFull + 1, PluginSlot::kMaxFullPlugins));
      }
      slots.push_back({ false, static_cast<uint16_t>(numFull++) });
    }
  }

  std::vector<std::string> fileNames;
  fileNames.reserve(pImpl->sources.size());

  for (size_t i = 0; i < pImpl->sources.size(); ++i) {
    auto& src = pImpl->sources[i];

    const auto tes4 = Convert<TES4>(src.br->LookupById(0));
    espm::CompressedFieldsCache dummyCache;
    const auto masters = tes4->GetData(dummyCache).masters;

    auto toComb = std::make_unique<IdMapping>();
    auto toRaw = std::make_unique<RawIdMapping>();
    size_t m = 0;
    for (m = 0; m < masters.size(); ++m) {
      const int globalIdx = pImpl->GetFileIndex(masters[m]);
      if (globalIdx == -1) {
        throw CombineError(src.fileName + " has unresolved dependency (" +
                           masters[m] + ")");
      }
      toComb->Set(static_cast<uint8_t>(m), slots[globalIdx]);
      toRaw->Set(slots[globalIdx], static_cast<uint8_t>(m));
    }
    toComb->Set(static_cast<uint8_t>(m), slots[i]);
    toRaw->Set(slots[i], static_cast<uint8_t>(m));

    // A form under a light plugin has 12 bits of its own and the game drops
    // the rest, so a form above 0xFFF under a light plugin (the plugin's own,
    // or one a plugin adds or changes under a light master) collides with
    // another form in the game, and looked up by the id the game gives it,
    // it would not be found here. Measured on 5 Oct over the 102 plugins the
    // Thornswood client loads: no such form, the highest under a light plugin
    // being 0xFE8 in JK's Castle Volkihar.esp. A collection with one is
    // broken in a way the game hides, so the server names it.
    for (size_t fileTopByte = 0; fileTopByte <= m; ++fileTopByte) {
      const auto slot = toComb->GetSlot(static_cast<uint8_t>(fileTopByte));
      if (!slot || !slot->light) {
        continue;
      }
      const uint32_t highest =
        src.br->GetHighestLocalId(static_cast<uint8_t>(fileTopByte));
      if (highest > PluginSlot::kLightLocalIdMask) {
        throw CombineError(fmt::format(
          "{} holds form {:#010x} under a light plugin, where the game keeps "
          "only the low 12 bits of an id and so cannot tell it from another "
          "form. Compact the plugin in xEdit, or take it out",
          src.fileName,
          (static_cast<uint32_t>(fileTopByte) << 24) | highest));
      }
    }

    src.toComb = std::move(toComb);
    src.toRaw = std::move(toRaw);
    fileNames.push_back(src.fileName);
  }

  pImpl->loadOrder = LoadOrder(std::move(fileNames), std::move(slots));

  std::unique_ptr<espm::CombineBrowser> res(new CombineBrowser);
  res->pImpl = pImpl;
  return res;
}

Combiner::~Combiner() = default;

}
