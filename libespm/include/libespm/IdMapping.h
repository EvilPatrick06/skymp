#pragma once
#include "LoadOrder.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace espm {

// From the ids written inside one plugin file to the ids the game gives
// them. Inside a file the top byte of an id indexes that file's own MAST
// list, and the index one past the last master is the file itself. Each of
// those can be a full or a light plugin, so a file's top byte maps to a slot
// (LoadOrder.h), not to another top byte.
class IdMapping
{
public:
  void Set(uint8_t fileTopByte, PluginSlot slot) noexcept;
  std::optional<PluginSlot> GetSlot(uint8_t fileTopByte) const noexcept;

  // An id whose top byte names no plugin comes back as
  // 0xFF000000 | (id & 0xFFFFFF), at or above 0xFF000000 where no plugin's
  // form can be. Callers have tested for that since before light plugins.
  uint32_t Map(uint32_t fileId) const noexcept;

private:
  std::array<std::optional<PluginSlot>, 256> slots;
};

// The other way: from an id the game gives a form to the id one plugin file
// writes for it, when that file can name it at all, that is when the form
// belongs to one of its masters or to the file itself.
class RawIdMapping
{
public:
  RawIdMapping() noexcept;

  void Set(PluginSlot slot, uint8_t fileTopByte);

  // An id this file cannot name comes back at or above 0xFF000000, the same
  // way IdMapping::Map answers.
  uint32_t Map(uint32_t id) const noexcept;

private:
  static constexpr uint16_t kNotAMaster = 0xFFFF;

  std::array<uint16_t, PluginSlot::kMaxFullPlugins> fullToFile;
  std::vector<uint16_t> lightToFile;
};

}
