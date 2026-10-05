#include "libespm/IdMapping.h"

namespace espm {

namespace {
uint32_t Unmapped(uint32_t id) noexcept
{
  return (PluginSlot::kCreatedTopByte << 24) |
    (id & PluginSlot::kFullLocalIdMask);
}
}

void IdMapping::Set(uint8_t fileTopByte, PluginSlot slot) noexcept
{
  slots[fileTopByte] = slot;
}

std::optional<PluginSlot> IdMapping::GetSlot(
  uint8_t fileTopByte) const noexcept
{
  return slots[fileTopByte];
}

uint32_t IdMapping::Map(uint32_t fileId) const noexcept
{
  const auto& slot = slots[fileId >> 24];
  if (!slot) {
    return Unmapped(fileId);
  }
  return slot->ToId(fileId);
}

RawIdMapping::RawIdMapping() noexcept
{
  fullToFile.fill(kNotAMaster);
}

void RawIdMapping::Set(PluginSlot slot, uint8_t fileTopByte)
{
  if (!slot.light) {
    fullToFile.at(slot.index) = fileTopByte;
    return;
  }
  if (slot.index >= lightToFile.size()) {
    lightToFile.resize(slot.index + 1, kNotAMaster);
  }
  lightToFile[slot.index] = fileTopByte;
}

uint32_t RawIdMapping::Map(uint32_t id) const noexcept
{
  const auto slot = PluginSlot::Of(id);
  if (!slot) {
    return Unmapped(id);
  }
  uint16_t fileTopByte = kNotAMaster;
  if (slot->light) {
    if (slot->index < lightToFile.size()) {
      fileTopByte = lightToFile[slot->index];
    }
  } else if (slot->index < fullToFile.size()) {
    fileTopByte = fullToFile[slot->index];
  }
  if (fileTopByte == kNotAMaster) {
    return Unmapped(id);
  }
  return (static_cast<uint32_t>(fileTopByte) << 24) |
    (id & slot->LocalIdMask());
}

}
