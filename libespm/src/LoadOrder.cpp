#include "libespm/LoadOrder.h"
#include <cctype>
#include <stdexcept>
#include <utility>

namespace espm {

uint32_t PluginSlot::LocalIdMask() const noexcept
{
  return light ? kLightLocalIdMask : kFullLocalIdMask;
}

uint32_t PluginSlot::ToId(uint32_t localId) const noexcept
{
  if (light) {
    return (kLightTopByte << 24) | (static_cast<uint32_t>(index) << 12) |
      (localId & kLightLocalIdMask);
  }
  return (static_cast<uint32_t>(index) << 24) | (localId & kFullLocalIdMask);
}

std::optional<PluginSlot> PluginSlot::Of(uint32_t id) noexcept
{
  const uint32_t topByte = id >> 24;
  if (topByte == kCreatedTopByte) {
    return std::nullopt;
  }
  if (topByte == kLightTopByte) {
    return PluginSlot{ true, static_cast<uint16_t>((id >> 12) & 0xFFF) };
  }
  return PluginSlot{ false, static_cast<uint16_t>(topByte) };
}

bool IsLightPlugin(std::string_view fileName, uint32_t tes4Flags) noexcept
{
  if (tes4Flags & kTes4LightFlag) {
    return true;
  }
  constexpr std::string_view kEsl = ".esl";
  if (fileName.size() < kEsl.size()) {
    return false;
  }
  const auto ext = fileName.substr(fileName.size() - kEsl.size());
  for (size_t i = 0; i < kEsl.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(ext[i])) != kEsl[i]) {
      return false;
    }
  }
  return true;
}

LoadOrder::LoadOrder(std::vector<std::string> fileNames_,
                     std::vector<PluginSlot> slots_)
  : fileNames(std::move(fileNames_))
  , slots(std::move(slots_))
{
  if (fileNames.size() != slots.size()) {
    throw std::invalid_argument("LoadOrder: one slot per plugin");
  }
  fullPositions.fill(kNoPosition);
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto& slot = slots[i];
    if (slot.light) {
      if (slot.index != lightPositions.size() ||
          lightPositions.size() >= PluginSlot::kMaxLightPlugins) {
        throw std::invalid_argument(
          "LoadOrder: " + fileNames[i] +
          " is not given the next light index in load order");
      }
      lightPositions.push_back(static_cast<int32_t>(i));
    } else {
      if (slot.index != numFull || numFull >= PluginSlot::kMaxFullPlugins) {
        throw std::invalid_argument(
          "LoadOrder: " + fileNames[i] +
          " is not given the next full index in load order");
      }
      fullPositions[numFull++] = static_cast<int32_t>(i);
    }
  }
}

LoadOrder LoadOrder::FullPlugins(std::vector<std::string> fileNames)
{
  std::vector<PluginSlot> slots(fileNames.size());
  for (size_t i = 0; i < slots.size(); ++i) {
    slots[i] = PluginSlot{ false, static_cast<uint16_t>(i) };
  }
  return LoadOrder(std::move(fileNames), std::move(slots));
}

std::optional<size_t> LoadOrder::FindByFileName(
  std::string_view fileName) const noexcept
{
  for (size_t i = 0; i < fileNames.size(); ++i) {
    if (fileNames[i] == fileName) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<size_t> LoadOrder::FindByFormId(uint32_t formId) const noexcept
{
  const auto slot = PluginSlot::Of(formId);
  if (!slot) {
    return std::nullopt;
  }
  int32_t position = kNoPosition;
  if (slot->light) {
    if (slot->index < lightPositions.size()) {
      position = lightPositions[slot->index];
    }
  } else if (slot->index < numFull) {
    position = fullPositions[slot->index];
  }
  if (position == kNoPosition) {
    return std::nullopt;
  }
  return static_cast<size_t>(position);
}

}
