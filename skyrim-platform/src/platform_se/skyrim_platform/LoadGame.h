#pragma once
#include "InitialInventory.h"

namespace SaveFile_ {
struct PlayerLocation;
struct RefID;
struct SaveFile;
struct ChangeForm;
struct ChangeFormNPC_;
struct Weather;
struct GlobalVariables;
class PluginRemap;
}

class LoadGame
{
public:
  class Time
  {
  public:
    void Set(uint8_t inS, uint8_t inM, uint8_t inH)
    {
      seconds = inS;
      minutes = inM;
      hours = inH;
      hasData = true;
    }

    bool IsSet(void) { return hasData; }

    uint8_t GetSeconds(void) { return seconds; }
    uint8_t GetMinutes(void) { return minutes; }
    uint8_t GetHours(void) { return hours; }

  private:
    uint8_t seconds = 0, minutes = 0, hours = 0;
    bool hasData = false;
  };

  static std::shared_ptr<SaveFile_::SaveFile> PrepareSaveFile(
    const char* pathInAssets);

  // Lists this game's plugins in the save, the given lists or else the
  // game's compiled ones, and returns how its form ids become the save's.
  // Every form id put into the save goes through it (Thornswood #1715).
  static SaveFile_::PluginRemap ListPlugins(
    SaveFile_::SaveFile& save, const std::vector<std::string>* loadOrder,
    const std::vector<std::string>* lightLoadOrder);

  // cellOrWorld is this game's id; plugins is what ListPlugins returned for
  // this save.
  static void Run(std::shared_ptr<SaveFile_::SaveFile> baseSavefile,
                  const std::array<float, 3>& pos,
                  const std::array<float, 3>& angle, uint32_t cellOrWorld,
                  Time* time, SaveFile_::Weather* _weather,
                  SaveFile_::ChangeFormNPC_* changeFormNPC,
                  const SaveFile_::PluginRemap& plugins,
                  const std::vector<InitialInventory::Item>* inventory);

  static std::wstring GetPathToMyDocuments();

private:
  static std::wstring StringToWstring(const std::string& s);

  static std::string GenerateGuid();

  static std::filesystem::path GetSaveFullPath(const std::string& name);

  static SaveFile_::PlayerLocation* FindSectionWithPlayerLocation(
    std::shared_ptr<SaveFile_::SaveFile> save);

  static SaveFile_::PlayerLocation CreatePlayerLocation(
    const std::array<float, 3>& pos, const SaveFile_::RefID& world);

  static std::vector<uint8_t> Decompress(
    const SaveFile_::ChangeForm& changeForm);

  static void EditChangeForm(std::vector<uint8_t>& data,
                             const std::array<float, 3>& pos,
                             const std::array<float, 3>& angle,
                             const SaveFile_::RefID& world);

  static std::vector<uint8_t> Compress(
    const std::vector<uint8_t>& uncompressed);

  static void WriteChangeForm(std::shared_ptr<SaveFile_::SaveFile> save,
                              SaveFile_::ChangeForm& changeForm,
                              const std::vector<uint8_t>& compressed,
                              size_t uncompressedSize);

  // saveCellOrWorld is the cell or worldspace as the save names it
  static void ModifyEssStructure(std::shared_ptr<SaveFile_::SaveFile> save,
                                 std::array<float, 3> pos,
                                 std::array<float, 3> angle,
                                 uint32_t saveCellOrWorld,
                                 const std::vector<InitialInventory::Item>* inventory);

  static void ModifySaveTime(std::shared_ptr<SaveFile_::SaveFile>& save,
                             Time* time);

  static void ModifySaveWeather(std::shared_ptr<SaveFile_::SaveFile>& save,
                                SaveFile_::Weather* _weather);

  static void ModifyPlayerFormNPC(std::shared_ptr<SaveFile_::SaveFile> save,
                                  SaveFile_::ChangeFormNPC_* changeFormNPC);

  static void FillChangeForm(
    std::shared_ptr<SaveFile_::SaveFile> save, SaveFile_::ChangeForm* form,
    std::pair<uint32_t, std::vector<uint8_t>>& newValues);
};
