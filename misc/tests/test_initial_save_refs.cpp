#include "savefile/SFStructure.h"
#include <cassert>
#include <stdexcept>
int main() {
  SaveFile_::SaveFile save{};
  // A Special Edition save at form version 78, which lists light plugins, so
  // its form id array can hold the light form 0xfe012abc.
  save.header.version=12; save.formVersion=78;
  save.formIDArray = {0x0001be1a, 0x2d19bad4, 0x01012345, 0xfe012abc, 0xff001234};
  save.formIDArrayCount=5; save.fileLocationTable.unknownTable3Offset=123;
  const auto original=save.formIDArray;
  auto ref=SaveFile_::RefID::CreateRefId(save,0x2d012345);
  assert(save.formIDArray.size()==6);
  assert(std::equal(original.begin(),original.end(),save.formIDArray.begin()));
  assert(save.formIDArray.back()==0x2d012345);
  assert(ref.byte0==0 && ref.byte1==0 && ref.byte2==6);
  assert(save.fileLocationTable.unknownTable3Offset==127);
  ref=SaveFile_::RefID::CreateRefId(save,0xfe012abc);
  assert(save.formIDArray.size()==6 && ref.byte2==4);
  save.formIDArray.resize(65535,0);save.formIDArrayCount=65535;
  ref=SaveFile_::RefID::CreateRefId(save,0x00abcdef);
  assert(ref.byte0==1 && ref.byte1==0 && ref.byte2==0);
  // A Legendary Edition save has no light plugin list, so a light form
  // would name nothing there.
  SaveFile_::SaveFile legendary{}; legendary.header.version=9; legendary.formVersion=74;
  bool lightRejected=false;try{SaveFile_::RefID::CreateRefId(legendary,0xfe012abc);}catch(const std::runtime_error&){lightRejected=true;}
  assert(lightRejected && legendary.formIDArray.empty());
  SaveFile_::SaveFile malformed{}; malformed.formIDArrayCount=1;
  bool rejected=false;try{SaveFile_::RefID::CreateRefId(malformed,1);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
}
