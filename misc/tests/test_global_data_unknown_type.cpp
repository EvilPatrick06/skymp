// savefile stops writing a global data block of a type it does not know, and
// names the type.
//
// Writer::WriteGlobalDataTable ended in assert(0) for a type outside 0 to 8,
// 100 to 114 and 1000 to 1005, which a Release build compiles out, and then
// wrote the save on without the block (Thornswood #2001). It now throws with
// the type.
//
// The template is read, its body set to be written plain (savefile writes no
// LZ4 body), the type of its first global data block set to 4242, and the save
// written.
#include "savefile/SFReader.h"
#include "savefile/SFWriter.h"
#include <cstdio>
#include <stdexcept>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static int Run(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  const std::string work = argv[2];

  auto save = SaveFile_::Reader(argv[1]).GetStructure();
  CHECK(save != nullptr && !save->globalDataTable1.empty());
  if (!save || save->globalDataTable1.empty()) return 1;

  save->header.compressionType = 0;

  // Unchanged, it writes.
  CHECK(SaveFile_::Writer(save).CreateSaveFile(work + "/unchanged.ess"));

  save->globalDataTable1[0].type = 4242;
  std::string said;
  try {
    SaveFile_::Writer(save).CreateSaveFile(work + "/type-4242.ess");
  } catch (std::runtime_error& e) {
    said = e.what();
  }
  CHECK(said == "savefile writes no global data of type 4242");
  if (!said.empty()) std::printf("  said: %s\n", said.c_str());

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS savefile stops writing global data of type 4242 and names it\n");
  return 0;
}

int main(int argc, char** argv)
{
  try {
    return Run(argc, argv);
  } catch (std::exception& e) {
    std::printf("FAIL threw: %s\n", e.what());
    return 1;
  }
}