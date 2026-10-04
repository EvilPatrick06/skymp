// The plugins the server loads are read into memory once, at startup, and
// never read from their files again (Thornswood #1602). Built and run by
// test_plugins_in_memory.js.
#include "AllocatedBuffer.h"
#include "MappedBuffer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;

static void Check(bool ok, const char* what)
{
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

static void Write(const std::filesystem::path& path, const std::string& bytes)
{
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Rewrites the file in place, the same length. False when the file cannot be
// opened for writing (on Windows, while a mapping holds it).
static bool Rewrite(const std::filesystem::path& path, const std::string& bytes)
{
  std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!f) {
    return false;
  }
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  f.flush();
  return f.good();
}

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::printf("FAIL  no scratch file given\n");
    return 2;
  }
  const std::filesystem::path path = argv[1];
  // Several pages, so a mapping would fault more than one in.
  const std::string startup(1 << 16, 'a');
  const std::string later(1 << 16, 'b');

  Write(path, startup);
  {
    Viet::AllocatedBuffer held(path);
    Check(Rewrite(path, later),
          "the plugin file is free once the server has read it");
    Check(held.GetLength() == startup.size() &&
            std::memcmp(held.GetData(), startup.data(), startup.size()) == 0,
          "the copy in memory is what was read at startup; nothing is read "
          "from the file afterwards");
  }

  // The way it was, for contrast: a mapped plugin is still read from its
  // file after startup, which is what stalled the loop at every new cell.
  Write(path, startup);
  {
    Viet::MappedBuffer mapped(path);
    const bool rewrote = Rewrite(path, later);
    const bool fromFile = !rewrote ||
      std::memcmp(mapped.GetData(), later.data(), later.size()) == 0;
    Check(fromFile,
          "a mapped plugin is still read from its file after startup (the old "
          "way)");
  }
  return failures ? 1 : 0;
}
