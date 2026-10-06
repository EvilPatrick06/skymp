#include "savefile/SFStructure.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
std::string Hex(uint32_t value)
{
  char text[11];
  std::snprintf(text, sizeof(text), "0x%08X", value);
  return text;
}

// The game matches plugin names without regard to case.
bool SamePlugin(const std::string& a, const std::string& b)
{
  return a.size() == b.size() &&
    std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
             std::tolower(static_cast<unsigned char>(y));
         });
}

// Where each of the game's plugins is in the save's list, adding the ones
// it does not hold after its last.
std::vector<uint32_t> ListInto(std::vector<std::string>& saveList,
                               const std::vector<std::string>& gameList,
                               size_t capacity, const char* kind)
{
  std::vector<uint32_t> res;
  for (auto& plugin : gameList) {
    auto it = std::find_if(
      saveList.begin(), saveList.end(),
      [&](const std::string& name) { return SamePlugin(name, plugin); });
    if (it == saveList.end()) {
      if (saveList.size() >= capacity)
        throw std::runtime_error(
          std::string("The save cannot list more than ") +
          std::to_string(capacity) + " " + kind + " plugins; " + plugin +
          " would be one more");
      saveList.push_back(plugin);
      it = saveList.end() - 1;
    }
    res.push_back(static_cast<uint32_t>(it - saveList.begin()));
  }
  return res;
}
}

SaveFile_::RefID SaveFile_::RefID::CreateRefId(SaveFile& parentSaveFile,
                                               uint32_t formId)
{
  if (parentSaveFile.formIDArrayCount != parentSaveFile.formIDArray.size())
    throw std::runtime_error("Inconsistent save FormID array count");
  // A light plugin's form id names the plugin by its place in the save's
  // light plugin list, which only Special Edition saves at form version 78
  // carry. Without it the id would name nothing.
  if ((formId >> 24) == 0xFE && !parentSaveFile.HasLightPluginInfo())
    throw std::runtime_error(
      "Form " + Hex(formId) + " is from a light plugin and this save (version " +
      std::to_string(parentSaveFile.header.version) + ", form version " +
      std::to_string(parentSaveFile.formVersion) +
      ") has no light plugin list");
  auto existing = parentSaveFile.FindIndexInFormIdArray(formId);
  uint32_t index;
  if (existing >= 0) {
    index = static_cast<uint32_t>(existing) + 1;
  } else {
    // RefIDs reserve their upper two bits for the namespace.
    if (parentSaveFile.formIDArray.size() >= 0x3fffff)
      throw std::runtime_error("Save FormID array is full");
    parentSaveFile.formIDArray.push_back(formId);
    parentSaveFile.formIDArrayCount = static_cast<uint32_t>(parentSaveFile.formIDArray.size());
    parentSaveFile.fileLocationTable.unknownTable3Offset += 4;
    index = parentSaveFile.formIDArrayCount;
  }
  RefID res;
  res.byte0 = static_cast<uint8_t>(index >> 16);
  res.byte1 = static_cast<uint8_t>(index >> 8);
  res.byte2 = static_cast<uint8_t>(index);

  return res;
}

SaveFile_::ChangeForm* SaveFile_::SaveFile::GetChangeFormByRefID(
  SaveFile_::RefID refID, const uint8_t& type)
{
  for (auto& form : this->changeForms) {
    if ((form.type & 0b00111111) == type &&
        form.formID == refID) /// Upper 2 bits represent the size of the data
                              /// lengths: zero them
      return &form;
  }
  return nullptr;
}

SaveFile_::GlobalVariables::GlobalVariable*
SaveFile_::SaveFile::GetGlobalvariableByRefID(SaveFile_::RefID& refID)
{
  GlobalData& gData = this->globalDataTable1[GLOBAL_VARIABLES_INDEX];

  if (gData.type != GLOBAL_VARIABLES_INDEX)
    return nullptr;

  GlobalVariables* globalsVar =
    reinterpret_cast<GlobalVariables*>(gData.data.get());

  if (!globalsVar)
    return nullptr;

  for (auto& gVar : globalsVar->globals) {
    if (gVar.formID == refID) {
      return &gVar;
    }
  }
  return nullptr;
}

int64_t SaveFile_::SaveFile::FindIndexInFormIdArray(uint32_t refID)
{
  for (uint32_t i = 0; i < this->formIDArray.size(); ++i) {
    if (this->formIDArray[i] == refID) {
      return i;
    }
  }
  return -1;
}

size_t SaveFile_::SaveFile::ScreenshotSize() const
{
  return size_t(header.shotWidth) * header.shotHeight *
    (IsSpecialEdition() ? 4 : 3);
}

uint32_t SaveFile_::SaveFile::CalculatePluginInfoSize() const
{
  uint32_t size = 1;
  for (auto& plugin : pluginInfo.pluginsName)
    size += uint32_t(2 + plugin.size());
  if (HasLightPluginInfo()) {
    size += 2;
    for (auto& plugin : lightPluginInfo.pluginsName)
      size += uint32_t(2 + plugin.size());
  }
  return size;
}

SaveFile_::PluginRemap SaveFile_::SaveFile::ListPlugins(
  const std::vector<std::string>& gamePlugins,
  const std::vector<std::string>& gameLightPlugins)
{
  // Everything the save already names keeps naming its plugin: the save's
  // lists keep their order, and a plugin of the game's they do not hold is
  // added after their last. They used to be replaced by the game's lists,
  // and then every entry the save already held named whatever plugin the
  // game had at that place: the template lists Dragonborn.esm third and
  // every client Dawnguard.esm, so its 10,547 Dragonborn.esm, Dawnguard.esm
  // and HearthFires.esm entries each named another of the three.
  PluginRemap remap;
  remap.saveListsLightPlugins = HasLightPluginInfo();
  remap.saveVersion = header.version;
  remap.saveFormVersion = formVersion;
  remap.lightPlugins = gameLightPlugins;

  const uint32_t oldSize = pluginInfoSize;

  // 0xFE and 0xFF are the light and created namespaces, so regular plugins
  // end at 0xFD; a light index has 12 bits.
  for (auto index :
       ListInto(pluginInfo.pluginsName, gamePlugins, 0xFE, "regular"))
    remap.fullToSave.push_back(static_cast<uint8_t>(index));
  pluginInfo.numPlugins = static_cast<uint8_t>(pluginInfo.pluginsName.size());
  if (remap.saveListsLightPlugins) {
    for (auto index : ListInto(lightPluginInfo.pluginsName, gameLightPlugins,
                               0x1000, "light"))
      remap.lightToSave.push_back(static_cast<uint16_t>(index));
    lightPluginInfo.numPlugins =
      static_cast<uint16_t>(lightPluginInfo.pluginsName.size());
  }

  pluginInfoSize = CalculatePluginInfoSize();
  const uint32_t addSize = pluginInfoSize - oldSize;
  fileLocationTable.formIDArrayCountOffset += addSize;
  fileLocationTable.unknownTable3Offset += addSize;
  fileLocationTable.globalDataTable1Offset += addSize;
  fileLocationTable.globalDataTable2Offset += addSize;
  fileLocationTable.changeFormsOffset += addSize;
  fileLocationTable.globalDataTable3Offset += addSize;
  return remap;
}

uint32_t SaveFile_::PluginRemap::ToSaveFormId(uint32_t gameFormId) const
{
  const uint32_t top = gameFormId >> 24;
  if (top == 0xFF)
    return gameFormId; // made at runtime, which the save holds itself
  if (top == 0xFE) {
    const uint32_t index = (gameFormId >> 12) & 0xFFF;
    if (index >= lightPlugins.size())
      throw std::runtime_error("Form " + Hex(gameFormId) +
                               " names light plugin " + std::to_string(index) +
                               " and the game listed " +
                               std::to_string(lightPlugins.size()));
    if (!saveListsLightPlugins)
      throw std::runtime_error(
        "Form " + Hex(gameFormId) + " is from the light plugin " +
        lightPlugins[index] + ", and this save (version " +
        std::to_string(saveVersion) + ", form version " +
        std::to_string(saveFormVersion) +
        ") has no light plugin list to name it by; only a Special Edition "
        "save at form version 78 or later has one");
    return 0xFE000000 | (uint32_t(lightToSave[index]) << 12) |
      (gameFormId & 0xFFF);
  }
  if (top >= fullToSave.size())
    throw std::runtime_error("Form " + Hex(gameFormId) + " names plugin " +
                             std::to_string(top) + " and the game listed " +
                             std::to_string(fullToSave.size()));
  return (uint32_t(fullToSave[top]) << 24) | (gameFormId & 0x00FFFFFF);
}

void SaveFile_::SaveFile::CheckFormIdArrayPlugins() const
{
  for (auto formId : formIDArray) {
    const uint32_t top = formId >> 24;
    if (top == 0xFF)
      continue; // created by this save
    const bool named = top == 0xFE
      ? HasLightPluginInfo() &&
        ((formId >> 12) & 0xFFF) < lightPluginInfo.pluginsName.size()
      : top < pluginInfo.pluginsName.size();
    if (!named)
      throw std::runtime_error("Form id array entry " + Hex(formId) +
                               " names no plugin this save lists");
  }
}
