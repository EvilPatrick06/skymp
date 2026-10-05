#include "database_drivers/PrivateEconomyLedger.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
std::string Read(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), {});
}
void Check(bool value)
{
  if (!value) throw std::runtime_error("Private ledger assertion failed");
}
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  // The caller owns a unique temporary root; no game directory is touched.
  const fs::path root = fs::u8path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto dir = root / "private", served = root / "served";
  fs::create_directories(served);
  WritePrivateEconomyLedger(dir, served, "merchant", "first");
  Check(Read(dir / "merchant-ledger.json") == "first");
  Check(Read(dir / "merchant-ledger.initialized") == "1");
  WritePrivateEconomyLedger(dir, served, "merchant", "second");
  Check(Read(dir / "merchant-ledger.json") == "second");
  bool refused = false;
  try { WritePrivateEconomyLedger(served / "private", served, "merchant", "exposed"); }
  catch (const std::exception&) { refused = true; }
  Check(refused && !fs::exists(served / "private"));
  refused = false;
  try { WritePrivateEconomyLedger(dir, served, "../invalid", "bad"); }
  catch (const std::exception&) { refused = true; }
  Check(refused);
  // A failed replacement must retain the old document and not acknowledge it.
  fs::rename(dir / "merchant-ledger.json",dir / "previous.json");
  fs::create_directory(dir / "merchant-ledger.json");
  refused = false;
  try { WritePrivateEconomyLedger(dir, served, "merchant", "lost"); }
  catch (const std::exception&) { refused = true; }
  Check(refused && Read(dir / "previous.json") == "second");
  fs::remove(dir / "merchant-ledger.json");
  fs::rename(dir / "previous.json",dir / "merchant-ledger.json");
  fs::create_hard_link(dir / "merchant-ledger.json",dir / "merchant-ledger.json.tmp");
  refused=false;
  try { WritePrivateEconomyLedger(dir,served,"merchant","must not truncate old"); }
  catch(const std::exception&) {refused=true;}
  Check(refused && Read(dir / "merchant-ledger.json") == "second");
  fs::remove(dir / "merchant-ledger.json.tmp");
  fs::create_hard_link(dir / "merchant-ledger.json",served / "exposed-ledger.json");
  refused=false;
  try { WritePrivateEconomyLedger(dir,served,"merchant","private"); }
  catch(const std::exception&) {refused=true;}
  Check(refused && Read(dir / "merchant-ledger.json") == "second");
  std::cout << "PASS private ledger durable replacement, initialization marker, failure and privacy\n";
}
