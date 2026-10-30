// WholeFileWrite::Write, the whole-file write the gamemode calls as
// mp.writeFileWhole (Thornswood #2740), on the real file system: a reader
// holding the old file keeps the old bytes, a new open gets the new file
// whole, a refusal throws and leaves the target as it was, and no new file is
// left beside it either way.
#include "WholeFileWrite.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  include <Windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

int failed = 0;

void Check(bool value, const std::string& what)
{
  std::cout << (value ? "PASS  " : "FAIL  ") << what << "\n";
  if (!value) {
    ++failed;
  }
}

std::string Read(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), {});
}

// What is beside the target that is not the target: a left over new file.
int Leftovers(const fs::path& dir)
{
  int n = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (WholeFileWrite::Detail::Utf8(entry.path().filename()).find(".whole-") !=
        std::string::npos) {
      ++n;
    }
  }
  return n;
}

bool Throws(const fs::path& target, const std::string& bytes,
            std::string* message = nullptr)
{
  try {
    WholeFileWrite::Write(target, bytes);
  } catch (const std::exception& e) {
    if (message) {
      *message = e.what();
    }
    return true;
  }
  return false;
}

#ifdef _WIN32
// A reader the way node opens a file (share read, write and delete), or one
// that does not share delete, as Windows PowerShell's Get-Content does.
struct Reader
{
  HANDLE h;
  Reader(const fs::path& path, bool shareDelete)
  {
    h = CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE |
                      (shareDelete ? FILE_SHARE_DELETE : 0),
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("the reader could not open the file");
    }
  }
  ~Reader() { CloseHandle(h); }
  std::string ReadAll()
  {
    std::string out;
    char buf[4096];
    DWORD got = 0;
    SetFilePointer(h, 0, nullptr, FILE_BEGIN);
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
      out.append(buf, got);
    }
    return out;
  }
};
#else
struct Reader
{
  int fd;
  Reader(const fs::path& path, bool) { fd = open(path.c_str(), O_RDONLY); }
  ~Reader() { close(fd); }
  std::string ReadAll()
  {
    std::string out;
    char buf[4096];
    lseek(fd, 0, SEEK_SET);
    for (ssize_t got; (got = read(fd, buf, sizeof(buf))) > 0;) {
      out.append(buf, static_cast<size_t>(got));
    }
    return out;
  }
};
#endif

}

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "usage: whole-file-write-test <a folder of its own>\n";
    return 2;
  }
  const fs::path root = fs::path(argv[1]) /
    std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto dir = root / "data";
  fs::create_directories(dir);
  const auto file = dir / "online.json";

  WholeFileWrite::Write(file, R"({"now":1})");
  Check(Read(file) == R"({"now":1})", "a file that was not there is written");

  WholeFileWrite::Write(file, R"({"now":12,"at":1})");
  Check(Read(file) == R"({"now":12,"at":1})",
        "a file that was there is replaced, longer");
  WholeFileWrite::Write(file, R"({"now":3})");
  Check(Read(file) == R"({"now":3})", "and shorter, with no old bytes after it");
  WholeFileWrite::Write(file, "");
  Check(fs::exists(file) && Read(file).empty(), "no bytes is an empty file");
  Check(Leftovers(dir) == 0, "no new file is left beside it");

  {
    WholeFileWrite::Write(file, "old bytes");
    Reader holder(file, true);
    std::string message;
    const bool threw = Throws(file, "new bytes, longer", &message);
    Check(!threw,
          "a reader that shares delete, as node opens a file, does not stop "
          "the write" +
            (threw ? " (" + message + ")" : std::string()));
    Check(holder.ReadAll() == "old bytes",
          "that reader keeps reading the old bytes");
    Check(Read(file) == "new bytes, longer", "a new open gets the new file");
  }
  Check(Leftovers(dir) == 0, "no new file is left beside it after that");

#ifdef _WIN32
  {
    WholeFileWrite::Write(file, "kept");
    Reader holder(file, false);
    std::string message;
    Check(Throws(file, "refused", &message),
          "a reader that does not share delete refuses the write, and it "
          "throws");
    Check(message.find("without sharing delete") != std::string::npos &&
            message.find("online.json") != std::string::npos,
          "the message names the file and why: " + message);
    Check(holder.ReadAll() == "kept", "the reader still has the file");
  }
  Check(Read(file) == "kept", "the refused write left the file as it was");
  Check(Leftovers(dir) == 0, "and left no new file beside it");
#endif

  {
    const auto folder = dir / "a-folder";
    fs::create_directory(folder);
    Check(Throws(folder, "x"), "a folder in its place throws");
    Check(fs::is_directory(folder), "and the folder is still there");
    Check(Leftovers(dir) == 0, "and no new file is left beside it");
  }
  Check(Throws(dir / "no-such-folder" / "x.json", "x"),
        "a folder that is not there throws");
  Check(Throws(fs::path(), "x"), "an empty path throws");

  fs::remove_all(root);
  if (failed) {
    std::cout << failed << " failed\n";
    return 1;
  }
  std::cout << "PASS whole-file write\n";
  return 0;
}
