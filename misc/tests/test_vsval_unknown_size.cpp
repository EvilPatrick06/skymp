// savefile stops on a vsval whose size bits are 3, and says so.
//
// A vsval's low two bits give its size: 0 one byte, 1 two, 2 four (UESP Save
// File Format, vsval). 3 is no size. Reader::ReadVsval_bit ended in assert(0)
// there, which a Release build compiles out, and then read every byte after
// it wrong (Thornswood #2001). It now throws with the size bits and the byte.
//
// The fixture is the given template with its body stored plain and the first
// byte of Global Variables (global data type 3, its global count, a vsval)
// given size bits 3. It is built here byte by byte.
#include "savefile/SFReader.h"
#include "SaveContainerFixture.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace F = SaveContainerFixture;
using Bytes = std::vector<uint8_t>;

static int Run(int argc, char** argv)
{
  if (argc != 3) { std::printf("usage: template.ess workdir\n"); return 2; }
  const std::string work = argv[2];
  auto c = F::Parse(F::ReadFile(argv[1]));

  Bytes file = c.flat;
  if (c.version >= 12) {
    const size_t at = c.headerEnd - size_t(c.shotWidth) * c.shotHeight * 4 - 2;
    file[at] = file[at + 1] = 0;
  }

  // Global data table 1 starts at flt[2] and holds flt[6] blocks.
  size_t block = c.flt[2], found = 0;
  for (uint32_t i = 0; i < c.flt[6]; ++i) {
    if (F::U32(file, block) == 3) { found = block; break; }
    block += 8 + F::U32(file, block + 4);
  }
  CHECK(found != 0);
  if (!found) return 1;

  // The plain template reads.
  const std::string plain = work + "/plain.ess";
  F::WriteFile(plain, file);
  CHECK(SaveFile_::Reader(plain).GetStructure() != nullptr);

  const uint8_t bad = static_cast<uint8_t>(file[found + 8] | 3);
  file[found + 8] = bad;
  const std::string fixture = work + "/vsval-size-3.ess";
  F::WriteFile(fixture, file);

  std::string said;
  try {
    SaveFile_::Reader reader(fixture);
  } catch (std::runtime_error& e) {
    said = e.what();
  }
  CHECK(said == "Vsval of unknown size type 3 (first byte " + std::to_string(bad) + ")");
  if (!said.empty()) std::printf("  said: %s\n", said.c_str());

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("PASS savefile stops on a vsval of size type 3 and names it\n");
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