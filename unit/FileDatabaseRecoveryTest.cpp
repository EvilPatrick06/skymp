#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sodium.h>

namespace {
struct RecoveryFixture
{
  std::filesystem::path root = std::filesystem::temp_directory_path() /
    ("thornswood-save-recovery-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));

  ~RecoveryFixture() { std::filesystem::remove_all(root); }

  MpChangeForm Actor(uint32_t id, uint32_t gold)
  {
    MpChangeForm form;
    form.formDesc = FormDesc(id, "");
    form.profileId = id;
    form.inv.AddItem(0xf, gold);
    return form;
  }

  std::map<uint32_t, uint32_t> Balances(FileDatabase& db)
  {
    std::map<uint32_t, uint32_t> balances;
    db.Iterate(
      [&](const MpChangeForm& form) {
        balances[form.formDesc.shortFormId] = form.inv.GetItemCount(0xf);
      },
      std::nullopt);
    return balances;
  }

  std::filesystem::path Journal()
  {
    return root / "changeForms/.pending-batch";
  }

  void Interrupt(FileDatabase& db)
  {
    auto second = root / "changeForms/2.json";
    auto backup = root / "second.before";
    std::filesystem::rename(second, backup);
    std::filesystem::create_directory(second);
    REQUIRE_THROWS(db.Upsert({ Actor(1, 90), Actor(2, 210) }));
    REQUIRE(std::filesystem::exists(Journal()));
    std::filesystem::remove(second);
    std::filesystem::rename(backup, second);
  }
};

std::string ReadBytes(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void WriteJournal(const std::filesystem::path& path, nlohmann::json journal)
{
  auto batch = journal.at("batch").dump();
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(digest,
                     reinterpret_cast<const unsigned char*>(batch.data()),
                     batch.size());
  char hex[crypto_hash_sha256_BYTES * 2 + 1];
  sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));
  journal["checksum"] = hex;
  std::ofstream file(path, std::ios::binary);
  file << journal.dump();
}
}

TEST_CASE("File saves recover the complete batch after a partial replacement",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  {
    FileDatabase db(fixture.root.string(), spdlog::default_logger());
    REQUIRE(db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) }) == 2);

    fixture.Interrupt(db);
  }

  FileDatabase restarted(fixture.root.string(), spdlog::default_logger());
  auto balances = fixture.Balances(restarted);
  REQUIRE(balances.size() == 2);
  REQUIRE(balances.at(1) == 90);
  REQUIRE(balances.at(2) == 210);
  REQUIRE(balances.at(1) + balances.at(2) == 300);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}

TEST_CASE("Failed journal preparation leaves actor balances unchanged",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) });
  auto temporary = fixture.root / "changeForms/.pending-batch.tmp";
  std::filesystem::create_directory(temporary);
  REQUIRE_THROWS(db.Upsert({ fixture.Actor(1, 90), fixture.Actor(2, 210) }));
  REQUIRE(fixture.Balances(db).at(1) == 100);
  REQUIRE(fixture.Balances(db).at(2) == 200);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}

TEST_CASE("A retry completes the older batch before preparing a new one",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) });
  fixture.Interrupt(db);
  auto temporary = fixture.root / "changeForms/.pending-batch.tmp";
  std::filesystem::create_directory(temporary);
  REQUIRE_THROWS(db.Upsert({ fixture.Actor(1, 80), fixture.Actor(2, 220) }));
  REQUIRE(fixture.Balances(db).at(1) == 90);
  REQUIRE(fixture.Balances(db).at(2) == 210);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
  std::filesystem::remove(temporary);
  REQUIRE(db.Upsert({ fixture.Actor(1, 80), fixture.Actor(2, 220) }) == 2);
  REQUIRE(fixture.Balances(db).at(1) == 80);
  REQUIRE(fixture.Balances(db).at(2) == 220);
}

TEST_CASE("Invalid live snapshots do not leave a poisoned recovery journal",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) });
  auto invalid = fixture.Actor(2, 210);
  invalid.position.x = std::numeric_limits<float>::quiet_NaN();
  REQUIRE_THROWS(db.Upsert({ fixture.Actor(1, 90), invalid }));
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
  REQUIRE(fixture.Balances(db).at(1) == 100);
  REQUIRE(fixture.Balances(db).at(2) == 200);
  REQUIRE(db.Upsert({ fixture.Actor(1, 90), fixture.Actor(2, 210) }) == 2);
}

TEST_CASE("Repeated actor snapshots retain the last submitted balance",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  REQUIRE(db.Upsert({ fixture.Actor(1, 100), std::nullopt,
                      fixture.Actor(1, 90), fixture.Actor(2, 210) }) == 3);
  REQUIRE(fixture.Balances(db).at(1) == 90);
  REQUIRE(fixture.Balances(db).at(2) == 210);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
  REQUIRE(db.Upsert({ std::nullopt }) == 0);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}

TEST_CASE("Reads recover pending batches before exposing saved actors",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) });
  fixture.Interrupt(db);
  auto balances = fixture.Balances(db);
  REQUIRE(balances.at(1) == 90);
  REQUIRE(balances.at(2) == 210);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}

TEST_CASE("Measure small and full population file save batches",
          "[.merchant-save-benchmark]")
{
  for (uint32_t count : { 25u, 5087u }) {
    RecoveryFixture fixture;
    FileDatabase db(fixture.root.string(), spdlog::default_logger());
    std::vector<std::optional<MpChangeForm>> forms;
    for (uint32_t id = 1; id <= count; ++id) {
      forms.push_back(fixture.Actor(id, 250));
    }
    auto start = std::chrono::steady_clock::now();
    REQUIRE(db.Upsert(std::move(forms)) == count);
    auto elapsed = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start)
                     .count();
    spdlog::info("File save batch: {} actors, {:.3f} ms", count, elapsed);
    REQUIRE(fixture.Balances(db).size() == count);
    REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
  }
}

TEST_CASE("Large batch failures retain every snapshot for recovery",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  std::vector<std::optional<MpChangeForm>> forms;
  for (uint32_t id = 1; id <= 300; ++id) {
    forms.push_back(fixture.Actor(id, 250));
  }
  auto second = fixture.root / "changeForms/2.json";
  auto backup = fixture.root / "second.before";
  {
    FileDatabase db(fixture.root.string(), spdlog::default_logger());
    REQUIRE(db.Upsert(std::vector<std::optional<MpChangeForm>>(forms)) == 300);
    std::filesystem::rename(second, backup);
    std::filesystem::create_directory(second);
    forms[0]->inv.entries[0].count = 240;
    forms[1]->inv.entries[0].count = 260;
    REQUIRE_THROWS(db.Upsert(std::move(forms)));
    REQUIRE(std::filesystem::exists(fixture.Journal()));
    REQUIRE_THROWS(
      FileDatabase(fixture.root.string(), spdlog::default_logger()));
    REQUIRE(std::filesystem::exists(fixture.Journal()));
    std::filesystem::remove(second);
    std::filesystem::rename(backup, second);
  }
  FileDatabase restarted(fixture.root.string(), spdlog::default_logger());
  auto balances = fixture.Balances(restarted);
  REQUIRE(balances.size() == 300);
  uint32_t total = 0;
  for (const auto& entry : balances) {
    total += entry.second;
  }
  REQUIRE(total == 75000);
  REQUIRE(balances.at(1) == 240);
  REQUIRE(balances.at(2) == 260);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}

#ifdef _WIN32
TEST_CASE("Windows case aliases coalesce before parallel writes",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  std::vector<std::optional<MpChangeForm>> forms;
  for (uint32_t id = 1; id <= 300; ++id) {
    forms.push_back(fixture.Actor(id, 250));
  }
  forms[0]->formDesc = FormDesc(1, "Skyrim.esm");
  forms[1]->formDesc = FormDesc(1, "skyrim.esm");
  forms[1]->inv.entries[0].count = 260;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  REQUIRE(db.Upsert(std::move(forms)) == 300);
  auto balances = fixture.Balances(db);
  REQUIRE(balances.size() == 299);
  REQUIRE(balances.at(1) == 260);
  REQUIRE_FALSE(std::filesystem::exists(fixture.Journal()));
}
#endif

TEST_CASE("Corrupt or invalid recovery batches refuse before any replacement",
          "[merchant-save]")
{
  RecoveryFixture fixture;
  FileDatabase db(fixture.root.string(), spdlog::default_logger());
  db.Upsert({ fixture.Actor(1, 100), fixture.Actor(2, 200) });
  fixture.Interrupt(db);
  auto journal = nlohmann::json::parse(ReadBytes(fixture.Journal()));
  auto firstBefore = ReadBytes(fixture.root / "changeForms/1.json");
  auto secondBefore = ReadBytes(fixture.root / "changeForms/2.json");
  // Make the first replay visibly different so validation-after-write fails.
  auto firstPayload =
    nlohmann::json::parse(journal["batch"][0]["json"].get<std::string>());
  firstPayload["inv"]["entries"][0]["count"] = 777;
  journal["batch"][0]["json"] = firstPayload.dump();

  SECTION("checksum mismatch")
  {
    journal["checksum"] = "broken";
    std::ofstream file(fixture.Journal());
    file << journal.dump();
  }
  SECTION("truncated journal")
  {
    std::ofstream file(fixture.Journal());
    file << "{\"version\":1,\"batch\":[";
  }
  SECTION("unsafe actor path")
  {
    journal["batch"][1]["file"] = "../escaped.json";
    WriteJournal(fixture.Journal(), journal);
  }
  SECTION("duplicate actor identity")
  {
    journal["batch"][1] = journal["batch"][0];
    WriteJournal(fixture.Journal(), journal);
  }
  SECTION("invalid actor payload")
  {
    journal["batch"][1]["json"] = "{\"formDesc\":\"2\",\"inv\":false}";
    WriteJournal(fixture.Journal(), journal);
  }
#ifdef _WIN32
  SECTION("invalid Windows filename")
  {
    auto payload =
      nlohmann::json::parse(journal["batch"][1]["json"].get<std::string>());
    payload["formDesc"] = "2:bad?.esm";
    journal["batch"][1]["json"] = payload.dump();
    journal["batch"][1]["file"] = "2_bad?.esm.json";
    WriteJournal(fixture.Journal(), journal);
  }
  SECTION("Windows alternate data stream")
  {
    auto payload =
      nlohmann::json::parse(journal["batch"][1]["json"].get<std::string>());
    payload["formDesc"] = "2:Skyrim.esm:stream";
    journal["batch"][1]["json"] = payload.dump();
    journal["batch"][1]["file"] = "2_Skyrim.esm:stream.json";
    WriteJournal(fixture.Journal(), journal);
  }
  SECTION("Windows control character")
  {
    auto payload =
      nlohmann::json::parse(journal["batch"][1]["json"].get<std::string>());
    payload["formDesc"] = "2:bad\x01.esm";
    journal["batch"][1]["json"] = payload.dump();
    journal["batch"][1]["file"] = "2_bad\x01.esm.json";
    WriteJournal(fixture.Journal(), journal);
  }
  SECTION("Windows case aliases")
  {
    for (size_t i = 0; i < 2; ++i) {
      auto payload =
        nlohmann::json::parse(journal["batch"][i]["json"].get<std::string>());
      payload["formDesc"] = i == 0 ? "1:Skyrim.esm" : "1:skyrim.esm";
      journal["batch"][i]["json"] = payload.dump();
      journal["batch"][i]["file"] =
        i == 0 ? "1_Skyrim.esm.json" : "1_skyrim.esm.json";
    }
    WriteJournal(fixture.Journal(), journal);
  }
#endif
  REQUIRE_THROWS(
    FileDatabase(fixture.root.string(), spdlog::default_logger()));
  size_t callbacks = 0;
  REQUIRE_THROWS(
    db.Iterate([&](const MpChangeForm&) { ++callbacks; }, std::nullopt));
  REQUIRE(callbacks == 0);
  REQUIRE(std::filesystem::exists(fixture.Journal()));
  REQUIRE(ReadBytes(fixture.root / "changeForms/1.json") == firstBefore);
  REQUIRE(ReadBytes(fixture.root / "changeForms/2.json") == secondBefore);
  REQUIRE_FALSE(std::filesystem::exists(fixture.root / "escaped.json"));
}
