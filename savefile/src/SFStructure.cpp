#include "savefile/SFStructure.h"

#include <cstring>
#include <stdexcept>

SaveFile_::RefID SaveFile_::RefID::CreateRefId(SaveFile& parentSaveFile,
                                               uint32_t formId)
{
  if (parentSaveFile.formIDArrayCount != parentSaveFile.formIDArray.size())
    throw std::runtime_error("Inconsistent save FormID array count");
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

void SaveFile_::SaveFile::OverwritePluginInfo(
  std::vector<std::string>& newPluginNames)
{
  uint32_t oldSize = this->pluginInfoSize;

  this->pluginInfoSize = 1;
  this->pluginInfo.numPlugins = 0;
  this->pluginInfo.pluginsName.clear();

  this->pluginInfo.numPlugins = static_cast<uint8_t>(newPluginNames.size());

  for (auto& plugin : newPluginNames) {
    this->pluginInfo.pluginsName.push_back(plugin);
    this->pluginInfoSize += uint32_t(2 + plugin.size());
  }

  uint32_t addSize = this->pluginInfoSize - oldSize;

  this->fileLocationTable.formIDArrayCountOffset += addSize;
  this->fileLocationTable.unknownTable3Offset += addSize;
  this->fileLocationTable.globalDataTable1Offset += addSize;
  this->fileLocationTable.globalDataTable2Offset += addSize;
  this->fileLocationTable.changeFormsOffset += addSize;
  this->fileLocationTable.globalDataTable3Offset += addSize;
}
