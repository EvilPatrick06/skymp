#pragma once
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <atomic>
#include <chrono>
#include <set>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace PrivateEconomyLedger {
inline void SyncDirectory(const std::filesystem::path& directory)
{
#ifndef _WIN32
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) throw std::runtime_error("Unable to open ledger directory");
  const int result = fsync(fd);
  close(fd);
  if (result != 0) throw std::runtime_error("Unable to sync ledger directory");
#endif
}
inline void Replace(const std::filesystem::path& path, const std::string& bytes)
{
  static std::atomic<unsigned long long> counter{0};
  auto temporary = path;
  temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
    "-" + std::to_string(counter++);
#ifdef _WIN32
  const int fd = _wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                        _S_IREAD | _S_IWRITE);
  if(fd < 0) throw std::runtime_error("Unable to exclusively create private ledger temporary file");
  auto file = _fdopen(fd, "wb");
#else
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if(fd < 0) throw std::runtime_error("Unable to exclusively create private ledger temporary file");
  auto file = fdopen(fd, "wb");
#endif
  struct Temporary {
    std::filesystem::path path;
    ~Temporary() {std::error_code ignored; std::filesystem::remove(path,ignored);}
  } owned{temporary};
  if (!file) {
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    throw std::runtime_error("Unable to open private ledger temporary file");
  }
  std::unique_ptr<FILE, decltype(&fclose)> guard(file, fclose);
  if (fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() || fflush(file) != 0)
    throw std::runtime_error("Unable to write private ledger");
#ifdef _WIN32
  const int result = _commit(_fileno(file));
#else
  const int result = fsync(fileno(file));
#endif
  if (result != 0) throw std::runtime_error("Unable to flush private ledger");
  if (fclose(guard.release()) != 0) throw std::runtime_error("Unable to close private ledger");
#ifdef _WIN32
  // Same-directory replacement, without COPY_ALLOWED. The move must reach
  // disk before its acknowledgement permits an inventory debit.
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Unable to replace private ledger");
#else
  std::filesystem::rename(temporary, path);
  SyncDirectory(path.parent_path());
#endif
}
inline bool ComponentEqual(const std::filesystem::path& a,
                           const std::filesystem::path& b)
{
#ifdef _WIN32
  const auto result = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
  if (!result) throw std::runtime_error("Unable to compare ledger paths");
  return result == CSTR_EQUAL;
#else
  return a == b;
#endif
}
inline bool Within(const std::filesystem::path& path,
                   const std::filesystem::path& resources)
{
  auto p = path.begin();
  for (auto r = resources.begin(); r != resources.end(); ++r, ++p) {
    if (p == path.end() || !ComponentEqual(*p, *r)) return false;
  }
  return true;
}
inline void CheckPrivate(const std::filesystem::path& input,
                         const std::filesystem::path& served)
{
  if(Within(std::filesystem::weakly_canonical(input),std::filesystem::weakly_canonical(served)))
    throw std::runtime_error("Economy ledger must be outside served resources");
}
inline void CheckServedAliases(const std::filesystem::path& directory,
                               const std::filesystem::path& served)
{
  const auto privateRoot=std::filesystem::weakly_canonical(directory);
  std::vector<std::filesystem::path> pending{served};
  std::set<std::filesystem::path> seen;
  size_t examined=0;
  while(!pending.empty()) {
    const auto next=std::filesystem::weakly_canonical(pending.back());pending.pop_back();
    if(Within(next,privateRoot) || Within(privateRoot,next))
      throw std::runtime_error("Served alias exposes private economy ledger");
    if(!std::filesystem::exists(next) || !seen.insert(next).second) continue;
    for(const auto& entry:std::filesystem::directory_iterator(next)) {
      if(++examined>65536) throw std::runtime_error("Served alias audit exceeds its bound");
      const auto resolved=std::filesystem::weakly_canonical(entry.path());
      if(Within(resolved,privateRoot) || Within(privateRoot,resolved))
        throw std::runtime_error("Served alias exposes private economy ledger");
      if(entry.is_directory()) pending.push_back(resolved);
    }
  }
}
inline void CreateDirectory(const std::filesystem::path& directory)
{
  std::vector<std::filesystem::path> missing;
  auto current=directory;
  while(!std::filesystem::exists(current)) {missing.push_back(current);current=current.parent_path();}
  for(auto it=missing.rbegin();it!=missing.rend();++it) {
    std::filesystem::create_directory(*it);
    SyncDirectory(it->parent_path());
  }
}
}

inline void WritePrivateEconomyLedger(const std::filesystem::path& directory,
                                     const std::filesystem::path& served,
                                     const std::string& name,
                                     const std::string& bytes)
{
  if (name != "merchant" && name != "dungeon-rewards")
    throw std::runtime_error("Unknown private economy ledger");
  if (bytes.size() > 64 * 1024 * 1024)
    throw std::runtime_error("Private economy ledger is too large");
  const auto dir = std::filesystem::absolute(directory);
  const auto resources = std::filesystem::absolute(served);
  const auto file = dir / (name + "-ledger.json");
  const auto marker = dir / (name + "-ledger.initialized");
  for (const auto& path : {dir, file, marker,
                          dir / (name + "-ledger.json.tmp"),
                          dir / (name + "-ledger.initialized.tmp")}) {
    PrivateEconomyLedger::CheckPrivate(path, resources);
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
       std::filesystem::is_regular_file(path) && std::filesystem::hard_link_count(path)>1)
      throw std::runtime_error("Private economy ledger file has an alias");
  }
  PrivateEconomyLedger::CheckServedAliases(dir,resources);
  PrivateEconomyLedger::CreateDirectory(dir);
  if (!std::filesystem::exists(marker))
    PrivateEconomyLedger::Replace(marker, "1");
  PrivateEconomyLedger::Replace(file, bytes);
}
