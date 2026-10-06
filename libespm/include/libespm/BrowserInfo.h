#pragma once
#include <cstdint>

namespace espm {

class CombineBrowser;

struct BrowserInfo
{
  BrowserInfo() = default;
  BrowserInfo(const CombineBrowser* parent_, uint16_t fileIdx_);

  // Returns 0 for empty (default constructed) LookupResult
  uint32_t ToGlobalId(uint32_t rawId) const noexcept;

  const CombineBrowser* const parent = nullptr;
  // Position of the plugin in the load order. A byte held 256 plugins; the
  // game holds up to 254 full and 4096 light ones.
  const uint16_t fileIdx = 0;
};

}
