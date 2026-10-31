#include "database_drivers/FileDatabase.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

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
    fs::path cwd = fs::current_path();
    ~Cleanup()
    {
      fs::current_path(cwd);
      fs::remove_all(path);
    }
  } cleanup{ fixture };
  size_t checks = 0;
  auto require = [&](bool condition) {
    ++checks;
    if (!condition) {
      throw std::runtime_error("FileDatabase path check " +
                               std::to_string(checks));
    }
  };
  try {
    fs::create_directories(fixture / "original");
    fs::create_directories(fixture / "elsewhere");
    fs::current_path(fixture / "original");
    FileDatabase db("relative", spdlog::default_logger());
    MpChangeForm form;
    form.formDesc = FormDesc(1, "");
    form.profileId = 1;
    form.inv.AddItem(0xf, 37);
    fs::current_path(fixture / "elsewhere");
    require(db.Upsert({ form }) == 1);
    size_t rows = 0;
    db.Iterate([&](const MpChangeForm& saved) {
      require(saved.formDesc == form.formDesc);
      require(saved.inv.GetItemCount(0xf) == 37);
      ++rows;
    }, std::nullopt);
    require(rows == 1);
    require(fs::exists(fixture / "original/relative/changeForms/1.json"));
    require(!fs::exists(fixture / "elsewhere/relative"));
    FileDatabase reopened((fixture / "original/relative").string(),
                          spdlog::default_logger());
    rows = 0;
    reopened.Iterate([&](const MpChangeForm& saved) {
      require(saved.inv.GetItemCount(0xf) == 37);
      ++rows;
    }, std::nullopt);
    require(rows == 1);
    std::cout << checks << " FileDatabase path checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
