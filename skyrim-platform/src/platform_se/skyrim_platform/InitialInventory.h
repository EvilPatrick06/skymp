#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// Only the embedded template is edited. Preserve quest/actor/animation data;
// inventory counts are reference deltas, not base-container totals.
namespace InitialInventory {
struct Item {
  std::array<uint8_t, 3> ref;
  int32_t delta;
  bool worn = false;
  bool wornLeft = false;
};
class Cursor {
public:
  explicit Cursor(const std::vector<uint8_t>& bytes) : bytes(bytes) {}
  size_t at = 0;
  void Skip(size_t count) {
    if (count > bytes.size() - at) throw std::runtime_error("Truncated template ACHR");
    at += count;
  }
  uint8_t Byte() { Skip(1); return bytes[at - 1]; }
  uint32_t Count() {
    auto first = Byte(); auto kind = first & 3;
    if (kind == 3) throw std::runtime_error("Invalid template VSVal");
    uint32_t value = first;
    for (unsigned i = 1; i <= kind; ++i) value |= uint32_t(Byte()) << (8 * i);
    return value >> 2;
  }
  void Extra(unsigned depth = 0) {
    if (depth > 16) throw std::runtime_error("Excessive template extra nesting");
    auto type = Byte();
    if (type == 4 || type == 8 || type == 12) {
      for (unsigned i = 0; i < type / 4; ++i) Extra(depth + 1);
    } else if (type == 22 || type == 23 || type == 0) {
    } else if (type == 142 || type == 112) {
      Skip(3);
    } else if (type == 136) {
      Skip(size_t(Count()) * 7); // Quest alias RefID + index.
    } else {
      throw std::runtime_error("Unsupported embedded template extra type");
    }
  }
  void Extras() {
    auto count = Count();
    if (count > 4096) throw std::runtime_error("Excessive template extra count");
    for (uint32_t i = 0; i < count; ++i) Extra();
  }
private:
  const std::vector<uint8_t>& bytes;
};
inline void Count(std::vector<uint8_t>& out, uint32_t value) {
  if (value > 0x3fffff) throw std::runtime_error("Inventory exceeds VSVal capacity");
  auto size = value <= 0x3f ? 1 : value <= 0x3fff ? 2 : 3;
  value = (value << 2) | (size - 1);
  for (int i = 0; i < size; ++i) out.push_back(uint8_t(value >> (8 * i)));
}
inline std::vector<uint8_t> Replace(const std::vector<uint8_t>& templateData,
                                    uint32_t flags, const std::vector<Item>& items) {
  // The template's player form: moved (initial type 4), inventory, leveled
  // inventory, animation and game only extras, and with or without the
  // encounter zone extra (CHANGE_REFR_EXTRA_ENCOUNTER_ZONE, 0x20000000; UESP
  // Skyrim Mod:ChangeFlags), which is one more entry of the extra list
  // Extras() reads. Measured: the Legendary Edition template 0xB8000022, a
  // new game SkyrimSE 1.6.1170 saved after player.additem (5 Oct 2026)
  // 0x98000022. Any other set may lay the form out otherwise, so it stops.
  constexpr uint32_t kEncounterZoneExtra = 0x20000000;
  if ((flags & ~kEncounterZoneExtra) != 0x98000022)
    throw std::runtime_error("Unexpected template ACHR flags");
  Cursor cursor(templateData);
  cursor.Skip(27 + 8); // location/rotation and two opaque actor integers.
  cursor.Extras();
  const auto begin = cursor.at;
  auto count = cursor.Count();
  if (count > 4096) throw std::runtime_error("Excessive template inventory");
  for (uint32_t i = 0; i < count; ++i) {
    cursor.Skip(3 + 4); cursor.Extras();
  }
  const auto end = cursor.at;
  std::vector<uint8_t> result(templateData.begin(), templateData.begin() + begin);
  Count(result, static_cast<uint32_t>(items.size()));
  for (const auto& item : items) {
    result.insert(result.end(), item.ref.begin(), item.ref.end());
    const auto delta = static_cast<uint32_t>(item.delta);
    for (int i = 0; i < 4; ++i) result.push_back(uint8_t(delta >> (i * 8)));
    if (item.worn || item.wornLeft) {
      // Each worn hand owns a distinct single-item stack.
      Count(result, unsigned(item.worn) + unsigned(item.wornLeft));
      if (item.worn) { Count(result, 1); result.push_back(22); }
      if (item.wornLeft) { Count(result, 1); result.push_back(23); }
    } else { Count(result, 0); }
  }
  result.insert(result.end(), templateData.begin() + end, templateData.end());
  return result;
}
}
