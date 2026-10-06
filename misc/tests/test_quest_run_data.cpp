// savefile reads and writes back unchanged a quest run whose event data holds
// an item of type 0.
//
// Quest Static Data (global data type 107) lists quest runs, each with event
// data items: a uint32 type, then a RefID for types 1, 2 and 4 and a uint32
// for type 3 (ReSaver ChangeFormQust.java, QuestRunDataItem3Data; UESP Save
// File Format/QUST Changeform). A new game SkyrimSE 1.6.1170 saved after
// player.additem on 5 Oct 2026 holds a run whose event is SCPT with the types
// 4, 2, 0, 0, 3, 3, and only a RefID after each type 0 reads that 1377 byte
// block to its end. savefile stopped at an assertion on type 0, so no such
// save could be the menu template.
//
// The fixture is the given template with its body stored plain and one more
// run put first in the block: event SCPT, a type 0 item naming the player
// (RefID 0x400014) and a type 3 item holding 7. It is built here byte by
// byte, so the same source builds against savefile before the change.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include "SaveContainerFixture.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace F = SaveContainerFixture;
using Bytes = std::vector<uint8_t>;

static void PutU32At(Bytes& b, size_t at, uint32_t v)
{
  std::memcpy(b.data() + at, &v, 4);
}

static int Run(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  const std::string work = argv[2];
  auto c = F::Parse(F::ReadFile(argv[1]));

  // The template with its body stored plain: the decompressed file with its
  // compression type set to none.
  Bytes file = c.flat;
  if (c.version >= 12) {
    const size_t at = c.headerEnd - size_t(c.shotWidth) * c.shotHeight * 4 - 2;
    file[at] = file[at + 1] = 0;
  }

  // Global data table 2 starts at flt[3] and holds flt[7] blocks.
  size_t block = c.flt[3], found = 0;
  for (uint32_t i = 0; i < c.flt[7]; ++i) {
    if (F::U32(file, block) == 107) { found = block; break; }
    block += 8 + F::U32(file, block + 4);
  }
  CHECK(found != 0);
  if (!found) return 1;

  Bytes run;
  F::PutU32(run, 4);
  run.insert(run.end(), { 'S', 'C', 'P', 'T' });
  F::PutU32(run, 2);
  F::PutU32(run, 0);
  run.insert(run.end(), { 0x40, 0x00, 0x14 });
  F::PutU32(run, 3);
  F::PutU32(run, 7);
  const uint32_t grow = static_cast<uint32_t>(run.size());

  const size_t data = found + 8;
  PutU32At(file, found + 4, F::U32(file, found + 4) + grow);
  PutU32At(file, data, F::U32(file, data) + 1);
  file.insert(file.begin() + data + 4, run.begin(), run.end());
  // Everything after table 2 moved: the form id array, the unknown table,
  // the change forms and table 3 (flt[0], [1], [4], [5]).
  for (int i : { 0, 1, 4, 5 }) PutU32At(file, c.fltAt + 4 * i, c.flt[i] + grow);

  const std::string fixture = work + "/quest-run-type0.ess";
  F::WriteFile(fixture, file);
  CHECK(F::CheckOffsets(F::Parse(file)).empty());

  auto save = SaveFile_::Reader(fixture).GetStructure();
  CHECK(save != nullptr);
  if (!save) return 1;
  const SaveFile_::QuestStaticData* quests = nullptr;
  for (auto& g : save->globalDataTable2)
    if (g.type == 107) quests = static_cast<const SaveFile_::QuestStaticData*>(g.data.get());
  CHECK(quests && !quests->unknowns0.empty());
  if (quests && !quests->unknowns0.empty()) {
    auto& items = quests->unknowns0[0].questRunData_items;
    CHECK(items.size() == 2);
    if (items.size() == 2) {
      CHECK(items[0].type == 0);
      auto ref = static_cast<const SaveFile_::RefID*>(items[0].unknown.get());
      CHECK(ref && ref->byte0 == 0x40 && ref->byte1 == 0x00 && ref->byte2 == 0x14);
      CHECK(items[1].type == 3);
      auto value = static_cast<const uint32_t*>(items[1].unknown.get());
      CHECK(value && *value == 7);
    }
  }

  const std::string out = work + "/quest-run-type0-out.ess";
  CHECK(SaveFile_::Writer(save).CreateSaveFile(out));
  CHECK(F::ReadFile(out) == file);

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS savefile reads a type 0 quest run data item and writes the save back byte for byte\n");
  return 0;
}

// An exception is a failure that says what it was, not a crash.
int main(int argc, char** argv)
{
  try {
    return Run(argc, argv);
  } catch (std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    return 1;
  }
}
