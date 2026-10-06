#include "WorldState.h"
#include "Appearance.h"
#include "FormCallbacks.h"
#include "MpActor.h"
#include "MpForm.h"
#include "MsgType.h"
#include "PartOne.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using Catch::Matchers::ContainsSubstring;

TEST_CASE("AddForm failures", "[WorldState]")
{
  WorldState worldState;
  worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0xff000000);
  REQUIRE_THROWS_WITH(
    worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0xff000000),
    ContainsSubstring("Form with id ff000000 already exists"));
}

TEST_CASE("DestroyForm failures", "[WorldState]")
{
  WorldState worldState;
  REQUIRE_THROWS_WITH(
    worldState.DestroyForm(0x12345678),
    ContainsSubstring("Form with id 12345678 doesn't exist"));

  worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0x12345678);
  REQUIRE_THROWS_WITH(
    worldState.DestroyForm<MpActor>(0x12345678),
    ContainsSubstring("Expected form 12345678 to be Actor, but got Form"));
}

TEST_CASE("Load ChangeForm of created Actor", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles =
    espm::LoadOrder::FullPlugins({ "Morrowind.esm", "Tribunal.esm" });

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.position = { 1, 2, 3 };
  changeForm.worldOrCellDesc = FormDesc::Tamriel();
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  auto& refr = worldState.GetFormAt<MpActor>(0xff000000);
  REQUIRE(refr.GetFormId() == 0xff000000);
  REQUIRE(refr.GetChangeForm().formDesc.ToString() == "0");
  REQUIRE(refr.GetPos() == NiPoint3{ 1, 2, 3 });
  REQUIRE(refr.GetCellOrWorld() == FormDesc::Tamriel());
  REQUIRE(refr.GetBaseId() == 0x0100abcd);
}

TEST_CASE("Load ChangeForm of created Actor with isDisabled=true",
          "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles =
    espm::LoadOrder::FullPlugins({ "Morrowind.esm", "Tribunal.esm" });

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.worldOrCellDesc = FormDesc::FromString("dead:Morrowind.esm");
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };
  changeForm.isDisabled = true;

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  auto& refr = worldState.GetFormAt<MpActor>(0xff000000);
  REQUIRE(refr.IsDisabled());

  // Disabled actors should not pollute grids during load process
  REQUIRE(worldState.GetGrids().count(0xdead) == 0);
}

TEST_CASE("Load ChangeForm of created Actor with profileId", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles =
    espm::LoadOrder::FullPlugins({ "Morrowind.esm", "Tribunal.esm" });

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.worldOrCellDesc = FormDesc::FromString("dead:Morrowind.esm");
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };
  changeForm.isDisabled = true;
  changeForm.profileId = 100;

  REQUIRE(worldState.GetActorsByProfileId(100).empty());
  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  REQUIRE(worldState.GetGrids().count(0xdead) == 0);
  REQUIRE(worldState.GetActorsByProfileId(100) ==
          std::set<uint32_t>({ 0xff000000 }));
}

TEST_CASE("Load ChangeForm of modified object", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = espm::LoadOrder::FullPlugins({ "Skyrim.esm" });

  MpChangeForm changeForm;
  changeForm.formDesc = { 0xeeee, "Skyrim.esm" };
  changeForm.position = { 1, 2, 3 };
  changeForm.worldOrCellDesc = FormDesc::Tamriel();
  changeForm.baseDesc = { 0xabcd, "Skyrim.esm" };

  auto newRefr = new MpObjectReference(
    LocationalData(), FormCallbacks::DoNothing(), 0x0000abcd, "STAT");
  worldState.AddForm(std::unique_ptr<MpObjectReference>(newRefr), 0xeeee);

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());
  auto& refr = worldState.GetFormAt<MpObjectReference>(0xeeee);
  REQUIRE(refr.GetFormId() == 0xeeee);
  REQUIRE(refr.GetChangeForm().formDesc.ToString() == "eeee:Skyrim.esm");
  REQUIRE(refr.GetPos() == NiPoint3{ 1, 2, 3 });
  REQUIRE(refr.GetCellOrWorld() == FormDesc::Tamriel());
  REQUIRE(refr.GetBaseId() == 0x0000abcd);
  REQUIRE(refr.Type() == std::string("ObjectReference"));
  REQUIRE(&refr == newRefr);
}

TEST_CASE("Load ChangeForm of modified object with changed baseType",
          "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = espm::LoadOrder::FullPlugins({ "Skyrim.esm" });
  auto newRefr = new MpObjectReference(
    LocationalData(), FormCallbacks::DoNothing(), 0x0000ded0, "STAT");
  worldState.AddForm(std::unique_ptr<MpObjectReference>(newRefr), 0xeeee);

  MpChangeForm changeForm;
  changeForm.formDesc = { 0xeeee, "Skyrim.esm" };
  changeForm.baseDesc = { 0xabcd, "Skyrim.esm" };

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  // Currently, baseId is not changed. I'm not sure if it should be changed.
  REQUIRE(newRefr->GetBaseId() == 0x0000ded0);

  // REQUIRE_THROWS_WITH(
  //   worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing()),
  //   ContainsSubstring("Anomally, baseId should never change (ded0 =>
  //   abcd)"));
}

extern PartOne& GetPartOne();

TEST_CASE("Loads VirtualMachine with all scripts", "[WorldState]")
{
  auto& p = GetPartOne();
  p.worldState.GetPapyrusVm();
}

TEST_CASE("HasEspmFile is working correctly", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = espm::LoadOrder::FullPlugins({ "file1", "file2" });
  REQUIRE(worldState.HasEspmFile("file1"));
  REQUIRE(worldState.HasEspmFile("file2"));
  REQUIRE_FALSE(worldState.HasEspmFile("BlowSkyrimModIndustry.exe"));
}

espm::Loader& GetEspmLoader();

namespace {
// A saved character whose appearance names a race the load order does not
// have as a RACE record. 0x7 is the player's NPC_ record, so it resolves to a
// record that is not a race, which is what LoadChangeForm refuses.
MpChangeForm CharacterWithUnreadableRace(WorldState& worldState,
                                         uint32_t formId, int32_t profileId)
{
  Appearance appearance;
  appearance.raceId = 0x7;
  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.formDesc = FormDesc::FromFormId(formId, worldState.espmFiles);
  changeForm.baseDesc = FormDesc::FromFormId(0x7, worldState.espmFiles);
  changeForm.worldOrCellDesc = FormDesc::FromString("3c:Skyrim.esm");
  changeForm.profileId = profileId;
  changeForm.appearanceDump = appearance.ToJson();
  return changeForm;
}
}

TEST_CASE("A saved character whose race cannot be read keeps its id",
          "[WorldState][espm]")
{
  // Thornswood #1269. The dev server skipped ff000005, ff00000a and ff00000d
  // at every start ("bad raceId in appearanceDump"), GenerateFormId saw their
  // ids as free, and the next two new characters were made as ff00000a and
  // ff00000d, which saved over the records on disk.
  WorldState worldState;
  worldState.AttachEspm(&GetEspmLoader(),
                        [] { return FormCallbacks::DoNothing(); });

  worldState.LoadChangeForm(
    CharacterWithUnreadableRace(worldState, 0xff000000, 964481319),
    FormCallbacks::DoNothing());
  worldState.LoadChangeForm(
    CharacterWithUnreadableRace(worldState, 0xff000002, -1),
    FormCallbacks::DoNothing());

  // not loaded, and not counted as the profile's character
  REQUIRE_FALSE(worldState.LookupFormById(0xff000000));
  REQUIRE_FALSE(worldState.LookupFormById(0xff000002));
  REQUIRE(worldState.GetActorsByProfileId(964481319).empty());

  // kept aside, with the profile it belongs to
  REQUIRE(worldState.GetUnreadableChangeForms() ==
          std::map<uint32_t, int32_t>{ { 0xff000000, 964481319 },
                                       { 0xff000002, -1 } });

  // and neither id is handed to a new form, so nothing is saved over them
  REQUIRE(worldState.GenerateFormId() == 0xff000001);
  REQUIRE(worldState.GenerateFormId() == 0xff000003);
  REQUIRE(worldState.GenerateFormId() == 0xff000004);
}

TEST_CASE("A saved character whose race can be read still loads",
          "[WorldState][espm]")
{
  WorldState worldState;
  worldState.AttachEspm(&GetEspmLoader(),
                        [] { return FormCallbacks::DoNothing(); });

  auto changeForm = CharacterWithUnreadableRace(worldState, 0xff000000, 77);
  Appearance appearance;
  appearance.raceId = 0x13746; // NordRace, Skyrim.esm
  changeForm.appearanceDump = appearance.ToJson();
  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  REQUIRE(worldState.LookupFormById(0xff000000));
  REQUIRE(worldState.GetActorsByProfileId(77) ==
          std::set<uint32_t>({ 0xff000000 }));
  REQUIRE(worldState.GetUnreadableChangeForms().empty());
  REQUIRE(worldState.GenerateFormId() == 0xff000001);
}
