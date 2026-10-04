#include "InitialInventory.h"
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "savefile/SFSeekerOfDifferences.h"
#include "InitialInventoryApiFixture.h"
#include "InitialInventoryApiUnderTest.inc"
#include <zlib.h>
class LoadGame { public:
 static std::vector<uint8_t> Compress(const std::vector<uint8_t>&);
 static std::vector<uint8_t> Decompress(const SaveFile_::ChangeForm&);
 static void WriteChangeForm(std::shared_ptr<SaveFile_::SaveFile>, SaveFile_::ChangeForm&,const std::vector<uint8_t>&,size_t);
};
#include "InitialSaveFunctionsUnderTest.inc"
#include <cassert>
#include <fstream>
#include <iterator>
void TestInventoryBuilder(std::shared_ptr<SaveFile_::SaveFile> save) {
  using nlohmann::json;
  RE::TESContainer base;
  for (auto id : {7u, 100u, 101u, 15u, 102u, 200u}) RE::TESForm::forms.emplace(id, RE::TESForm{id});
  RE::TESForm::forms.at(7).container = &base;
  std::vector<RE::ContainerObject> objects = {
    {RE::TESForm::LookupByID(100), 1}, {RE::TESForm::LookupByID(101), 1},
    {RE::TESForm::LookupByID(15), 23}, {RE::TESForm::LookupByID(102), 1}
  };
  for (auto& object : objects) base.containerObjects.push_back(&object);
  base.numContainerObjects = static_cast<uint32_t>(objects.size());
  auto desired = json::array({{{"baseId",100},{"count",1},{"worn",true}},
    {{"baseId",101},{"count",2}}, {{"baseId",15},{"count",40}},
    {{"baseId",200},{"count",1},{"worn",true}}, {{"baseId",200},{"count",1},{"wornLeft",true}}});
  auto result = CreateInitialInventory(save, Napi::Object(json{{"entries",desired}}));
  std::map<uint32_t, InitialInventory::Item> decoded;
  for (auto& item : *result) {
    auto index = (uint32_t(item.ref[0])<<16) | (uint32_t(item.ref[1])<<8) | item.ref[2];
    assert(index && index <= save->formIDArray.size());
    decoded.emplace(save->formIDArray[index-1], item);
  }
  assert(decoded.size()==5);
  assert(decoded.at(100).delta==0 && decoded.at(100).worn);
  assert(decoded.at(101).delta==1);
  assert(decoded.at(15).delta==17);
  assert(decoded.at(102).delta==-1);
  assert(decoded.at(200).delta==2 && decoded.at(200).worn && decoded.at(200).wornLeft);
  auto rejects = [&](json entries) {
    const auto refs = save->formIDArray;
    bool rejected=false;
    try { CreateInitialInventory(save, Napi::Object(json{{"entries",entries}})); }
    catch (const std::runtime_error&) { rejected=true; }
    assert(rejected);
    assert(save->formIDArray==refs); // Invalid inventory must not partially append IDs.
  };
  for (auto value : {1.5, 0.0, -1.0, 4294967297.0,
       std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    rejects(json::array({{{"baseId",100},{"count",value}}}));
  for (auto value : {100.5, 0.0, -1.0, 4294967396.0})
    rejects(json::array({{{"baseId",value},{"count",1}}}));
  rejects(json::array({{{"baseId",999},{"count",1}}}));
  rejects(json::array({{{"baseId",100},{"count",1},{"name","must not discard"}}}));
  rejects(json::array({{{"baseId",100},{"count",1},{"worn",1}}}));
  rejects(json::array({{{"baseId",200},{"count",1},{"worn",true},{"wornLeft",true}}}));
  rejects(json::array({{{"baseId",100},{"count",2},{"worn",true}}}));
  rejects(json::array({{{"baseId",100},{"count",1},{"worn",true}},{{"baseId",100},{"count",1},{"worn",true}}}));
  rejects(json::array({{{"baseId",200},{"count",INT32_MAX}},{{"baseId",200},{"count",1}}}));
  auto excessive = json::array();
  for (unsigned i=0;i<4097;++i) excessive.push_back({{"baseId",100},{"count",1}});
  rejects(excessive);
  auto decreased = CreateInitialInventory(save, Napi::Object(json{{"entries",json::array({{{"baseId",15},{"count",2}}})}}));
  bool found=false;
  for (auto& item : *decreased) if (item.delta == -21) found=true;
  assert(found);
  RE::TESForm::forms.clear();
}
int main(int argc, char** argv) {
  assert(argc == 3);
  auto save = SaveFile_::Reader(std::string(argv[1])).GetStructure();
  auto player = save->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::Player),1);
  assert(player);
  auto bytes = LoadGame::Decompress(*player);
  assert(!bytes.empty());
  TestInventoryBuilder(save);
  std::vector<InitialInventory::Item> entries = {
    {{0x41, 0xbe, 0x1a}, 1, true}, {{0x41, 0xbe, 0x1b}, 0, true},
    {{0x40, 0, 0x0f}, 40}, {{0x41, 0x39, 0x7d}, -1},
    {{0, 0, 1}, 1, false, true}, {{0, 0, 2}, 2, true, true}
  };
  auto output = InitialInventory::Replace(bytes, 0xb8000022, entries);
  InitialInventory::Cursor before(bytes), after(output);
  before.Skip(35); before.Extras(); const auto begin = before.at;
  after.Skip(35); after.Extras(); assert(after.at == begin);
  assert(after.Count() == entries.size());
  for (const auto& entry : entries) {
    for (auto ref : entry.ref) assert(after.Byte() == ref);
    uint32_t count = 0; for (int i = 0; i < 4; ++i) count |= uint32_t(after.Byte()) << (8*i);
    assert(static_cast<int32_t>(count) == entry.delta);
    assert(after.Count() == unsigned(entry.worn) + unsigned(entry.wornLeft));
    if (entry.worn) { assert(after.Count() == 1); assert(after.Byte() == 22); }
    if (entry.wornLeft) { assert(after.Count() == 1); assert(after.Byte() == 23); }
  }
  const auto originalCount = before.Count();
  for (unsigned i = 0; i < originalCount; ++i) { before.Skip(7); before.Extras(); }
  assert(std::vector<uint8_t>(bytes.begin()+before.at,bytes.end()) == std::vector<uint8_t>(output.begin()+after.at,output.end()));
  assert(std::equal(bytes.begin(),bytes.begin()+begin,output.begin()));
  for (auto n : {0u, 63u, 64u, 16383u, 16384u, 4194303u}) {
    std::vector<uint8_t> encoded; InitialInventory::Count(encoded,n); InitialInventory::Cursor read(encoded); assert(read.Count()==n);
  }
  auto originalRefs = save->formIDArray;
  auto pluginRef = SaveFile_::RefID::CreateRefId(*save,0x2d19bad4);
  assert(std::equal(originalRefs.begin(),originalRefs.end(),save->formIDArray.begin()));
  assert(save->formIDArray.back()==0x2d19bad4);
  LoadGame::WriteChangeForm(save,*player,LoadGame::Compress(output),output.size());
  assert(SaveFile_::Writer(save).CreateSaveFile(argv[2]));
  auto reread = SaveFile_::Reader(std::string(argv[2])).GetStructure();
  auto restored = reread->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::Player),1);
  assert(LoadGame::Decompress(*restored)==output);
  assert(reread->formIDArray==save->formIDArray);
  // Force a 2-byte to 4-byte length transition using incompressible bytes.
  std::vector<uint8_t> large(70000); uint32_t random=0x71ac9;
  for(auto& byte:large){random ^= random<<13; random ^= random>>17; random ^= random<<5;byte=uint8_t(random);}
  auto packed=LoadGame::Compress(large); assert(packed.size()>65535);
  LoadGame::WriteChangeForm(save,*player,packed,large.size());
  assert((player->type >> 6)==2);
  assert(SaveFile_::Writer(save).CreateSaveFile(argv[2]));
  reread=SaveFile_::Reader(std::string(argv[2])).GetStructure();
  restored=reread->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::Player),1);
  assert(LoadGame::Decompress(*restored)==large);
  // Widths must also shrink, then grow from one byte to two bytes.
  for (auto size : {32u, 300u}) {
    std::vector<uint8_t> sample(large.begin(),large.begin()+size);
    packed=LoadGame::Compress(sample);
    LoadGame::WriteChangeForm(save,*player,packed,sample.size());
    assert((player->type >> 6)==(size==32 ? 0 : 1));
    assert(SaveFile_::Writer(save).CreateSaveFile(argv[2]));
    reread=SaveFile_::Reader(std::string(argv[2])).GetStructure();
    restored=reread->GetChangeFormByRefID(SaveFile_::RefID(SaveFile_::RefID::Player),1);
    assert(LoadGame::Decompress(*restored)==sample);
  }
  bool rejected=false; try { InitialInventory::Replace({1,2,3},0xb8000022,entries); } catch (...) {rejected=true;} assert(rejected);
  rejected=false;uint8_t tiny[1];try{SaveFile_::SeekerOfDifferences::ZlibCompress(large.data(),large.size(),tiny,1);}catch(...){rejected=true;}assert(rejected);
}
