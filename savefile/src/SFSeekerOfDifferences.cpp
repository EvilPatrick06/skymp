
#include "savefile/SFSeekerOfDifferences.h"
#include "savefile/SFChangeFormNPC.h"

#include <bitset>
#include <iostream>

#include <zlib.h>

namespace {
// Example program

void ReportFlags(uint32_t v, std::ostream& out) noexcept
{
  auto bits = std::bitset<32>(v);
  auto s = bits.to_string();

  for (int i = 0; i < 3; ++i) {
    auto p = s.rfind(' ') + 1;
    s.insert(s.begin() + 8 + p, ' ');
  }
  out << s << "\n";
}
}

void SaveFile_::SeekerOfDifferences::ZlibDecompress(const uint8_t* in,
                                                    size_t inSize,
                                                    uint8_t* out,
                                                    size_t outSize)
{
  uLongf actual = static_cast<uLongf>(outSize);
  const int result = uncompress(out, &actual, in, static_cast<uLong>(inSize));
  if (result != Z_OK || actual != outSize)
    throw std::runtime_error("Invalid compressed save data");

}

size_t SaveFile_::SeekerOfDifferences::ZlibCompress(const uint8_t* in,
                                                    size_t inSize,
                                                    uint8_t* out,
                                                    size_t outMaxSize)
{
  uLongf actual = static_cast<uLongf>(outMaxSize);
  const int result = compress2(out, &actual, in, static_cast<uLong>(inSize), Z_BEST_COMPRESSION);
  if (result != Z_OK) throw std::runtime_error("Save compression did not finish");
  return actual;

}

SaveFile_::SeekerOfDifferences::ComparisonDifferences
SaveFile_::SeekerOfDifferences::StartCompare()
{

  auto& changeFormsFirstObject = firstObject->changeForms;
  auto& changeFormsSecondObject = secondObject->changeForms;

  ComparisonDifferences compareResult;

  for (auto& formObj1 : changeFormsFirstObject) {
    if (!formObj1.Is_ACHR_Type())
      continue;

    if (!formObj1.formID.IsPlayerID())
      continue;

    for (auto& formObj2 : changeFormsSecondObject) {

      if (formObj1.type != formObj2.type)
        continue;
      if (formObj1.formID != formObj2.formID)
        continue;

      /*firstObject->fileLocationTable.formIDArrayCountOffset -=
      formObj1.data.size();
      firstObject->fileLocationTable.formIDArrayCountOffset +=
      formObj2.data.size();

      firstObject->fileLocationTable.unknownTable3Offset -=
      formObj1.data.size(); firstObject->fileLocationTable.unknownTable3Offset
      += formObj2.data.size();

      firstObject->fileLocationTable.globalDataTable3Offset -=
      formObj1.data.size();
      firstObject->fileLocationTable.globalDataTable3Offset +=
      formObj2.data.size();

      formObj1.changeFlags = formObj2.changeFlags;
      formObj1.data = formObj2.data;
      formObj1.length1 = formObj2.length1;
      formObj1.length2 = formObj2.length2;
      formObj1.version = formObj2.version;*/

      /*std::array<Data, 2> result;
      std::array<ChangeForm*, 2> formObjs = { &formObj1, &formObj2 };

      for (int i = 0; i < 2; ++i) {
              result[i].changeFlags = formObjs[i]->changeFlags;
              result[i].value = formObjs[i]->data;

                      ReportFlags(formObjs[i]->changeFlags, std::cout);

              if (formObjs[i]->length2 != 0) {
                      std::vector<uint8_t> res;
                      res.resize(formObjs[i]->length2);
                      ZlibDecompress(formObjs[i]->data.data(),
      formObjs[i]->length1, res.data(), res.size());

                      result[i].value = res;
              }
              else {
                      result[i].value = formObjs[i]->data;
              }
      }

      compareResult.push_back(result);*/
      break;
    }
  }
  return compareResult;
}

void SaveFile_::SeekerOfDifferences::CoutVector(std::vector<uint8_t> vector,
                                                std::string nameObject)
{

  std::cout << nameObject << " be kept " << vector.size() << " bytes."
            << std::endl;

  for (auto& item : vector)
    Write(item);
}
