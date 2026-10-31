#include "database_drivers/SaveDirectory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
  if (argc != 2) {
    return 2;
  }
  const auto fixture = fs::absolute(argv[1]) /
    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  struct Cleanup
  {
    fs::path path;
    ~Cleanup() { fs::remove_all(path); }
  } cleanup{ fixture };
  size_t checks = 0;
  auto require = [&](bool condition) {
    ++checks;
    if (!condition) {
      throw std::runtime_error("Directory check " + std::to_string(checks));
    }
  };
  try {
    const auto leaf = fixture / "new/parent/changeForms";
    std::vector<fs::path> visited;
    auto collect = [&](const fs::path& path) { visited.push_back(path); };
    const auto actual =
      FileDatabaseDetail::InitializeDirectoryTree(leaf, collect);
    require(actual == fs::canonical(leaf));
    require(actual.is_absolute());
    std::vector<fs::path> expected;
    for (auto path = actual;; path = path.parent_path()) {
      expected.push_back(path);
      if (path == path.root_path()) {
        break;
      }
    }
    require(visited == expected);
    visited.clear();
    require(FileDatabaseDetail::InitializeDirectoryTree(leaf / ".", collect) ==
            actual);
    require(visited == expected);
    for (size_t failure : { size_t(0), size_t(1) }) {
      visited.clear();
      bool threw = false;
      try {
        FileDatabaseDetail::InitializeDirectoryTree(
          leaf, [&](const fs::path& path) {
            visited.push_back(path);
            if (visited.size() == failure + 1) {
              throw std::runtime_error("Injected barrier failure");
            }
          });
      } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "Injected barrier failure";
      }
      require(threw);
      require(visited.size() == failure + 1);
    }
    require(FileDatabaseDetail::InitializeDirectoryTree(leaf) == actual);
#ifndef _WIN32
    fs::create_directory_symlink(leaf.parent_path(), fixture / "alias");
    visited.clear();
    require(FileDatabaseDetail::InitializeDirectoryTree(
              fixture / "alias/changeForms", collect) == actual);
    require(visited == expected);
    const auto regular = fixture / "regular";
    std::ofstream(regular) << "not a directory";
    require(fs::is_regular_file(regular));
    bool rejected = false;
    try {
      FileDatabaseDetail::SyncDirectory(regular);
    } catch (const std::system_error&) {
      rejected = true;
    }
    require(rejected);
#endif
    std::cout << checks << " directory checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
