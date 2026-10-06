#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace espm {

// Where a plugin sits in the running game, and so what ids its forms get.
//
// Skyrim SE gives a full plugin a top byte of its own, 0x00 to 0xFD, and its
// forms the ids (index << 24) | (id & 0xFFFFFF). A light plugin has no top
// byte of its own: every light plugin shares 0xFE and is told apart by a
// 12 bit index under it, so its forms get
// 0xFE000000 | (index << 12) | (id & 0xFFF). Each index counts only plugins
// of its own kind, in load order. 0xFF is left for forms made at runtime,
// which on this server are the forms the server creates.
//
// Measured on 5 Oct (Thornswood #1715): in Patrick's dev session the client
// listed KhisartinBeards.esp 42nd among its light plugins (index 41, 0x029),
// and the game gave that plugin's beard head parts 0x827 and 0x838 the ids
// 0xFE029827 and 0xFE029838.
struct PluginSlot
{
  static constexpr uint32_t kLightTopByte = 0xFE;
  static constexpr uint32_t kCreatedTopByte = 0xFF;
  static constexpr uint32_t kMaxFullPlugins = 0xFE;    // 0x00 to 0xFD
  static constexpr uint32_t kMaxLightPlugins = 0x1000; // 0x000 to 0xFFF
  static constexpr uint32_t kFullLocalIdMask = 0x00FFFFFF;
  static constexpr uint32_t kLightLocalIdMask = 0x00000FFF;

  bool light = false;
  uint16_t index = 0;

  // The bits of an id that stay the same wherever the plugin loads.
  uint32_t LocalIdMask() const noexcept;

  // The id the game gives the form with this local id in this plugin. A
  // light plugin keeps only the low 12 bits, as the game does.
  uint32_t ToId(uint32_t localId) const noexcept;

  // The slot an id belongs to, or nothing for an id at or above 0xFF000000,
  // which no plugin owns.
  static std::optional<PluginSlot> Of(uint32_t id) noexcept;

  friend bool operator==(const PluginSlot& a, const PluginSlot& b) noexcept
  {
    return a.light == b.light && a.index == b.index;
  }
  friend bool operator!=(const PluginSlot& a, const PluginSlot& b) noexcept
  {
    return !(a == b);
  }
};

// The game's own rule for what is light: the ESL flag, 0x200, in the TES4
// record header, or an .esl extension, which the game loads as light with or
// without the flag. An .esp or .esm carrying the flag is light too. Measured
// on 5 Oct over the 102 plugins the Thornswood client loads: 54 are light,
// and 50 of those are .esp files with the flag, three are .esl files with the
// flag and no master flag, and one, _ResourcePack.esl, has both.
inline constexpr uint32_t kTes4LightFlag = 0x200;
bool IsLightPlugin(std::string_view fileName, uint32_t tes4Flags) noexcept;

// The plugins the server loaded, in load order, with the slot the game gives
// each. Descriptors ("827:KhisartinBeards.esp") and ids convert through
// this, so a light plugin's forms keep the ids the game gives them.
class LoadOrder
{
public:
  LoadOrder() = default;

  // Throws std::invalid_argument unless the slots are the ones the game
  // gives these plugins in this order: full indices counting up from 0 and
  // light indices counting up from 0, each in load order.
  LoadOrder(std::vector<std::string> fileNames,
            std::vector<PluginSlot> slots);

  // Every plugin named here is full. For callers that hold no plugin file to
  // read a header from, such as tests. Lightness lives in the header, so a
  // light plugin cannot be listed this way; use the constructor above.
  static LoadOrder FullPlugins(std::vector<std::string> fileNames);

  size_t size() const noexcept { return fileNames.size(); }
  bool empty() const noexcept { return fileNames.empty(); }
  const std::string& operator[](size_t i) const { return fileNames.at(i); }
  std::vector<std::string>::const_iterator begin() const noexcept
  {
    return fileNames.begin();
  }
  std::vector<std::string>::const_iterator end() const noexcept
  {
    return fileNames.end();
  }
  const std::vector<std::string>& GetFileNames() const noexcept
  {
    return fileNames;
  }

  PluginSlot GetSlot(size_t i) const { return slots.at(i); }

  // Position in the load order of the plugin named exactly so.
  std::optional<size_t> FindByFileName(
    std::string_view fileName) const noexcept;

  // Position in the load order of the plugin that owns this id, or nothing
  // when no loaded plugin does: an id the server made (0xFF), or a slot no
  // plugin fills.
  std::optional<size_t> FindByFormId(uint32_t formId) const noexcept;

  size_t GetNumFullPlugins() const noexcept { return numFull; }
  size_t GetNumLightPlugins() const noexcept { return lightPositions.size(); }

private:
  static constexpr int32_t kNoPosition = -1;

  std::vector<std::string> fileNames;
  std::vector<PluginSlot> slots;
  size_t numFull = 0;
  std::array<int32_t, PluginSlot::kMaxFullPlugins> fullPositions{};
  std::vector<int32_t> lightPositions;
};

}
