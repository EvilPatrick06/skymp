#pragma once
#include <cerrno>
#include <filesystem>
#include <system_error>
#ifndef _WIN32
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace FileDatabaseDetail {
inline void SyncDirectory(const std::filesystem::path& directory)
{
#ifndef _WIN32
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "Unable to open save directory for sync");
  }
  const int syncError = fsync(fd) == 0 ? 0 : errno;
  const int closeError = close(fd) == 0 ? 0 : errno;
  if (syncError || closeError) {
    throw std::system_error(syncError ? syncError : closeError,
                            std::generic_category(),
                            "Unable to sync or close save directory");
  }
#else
  // This existing no-op does not certify Windows directory persistence.
  (void)directory;
#endif
}

template <class Sync = decltype(&SyncDirectory)>
std::filesystem::path InitializeDirectoryTree(
  const std::filesystem::path& directory, Sync sync = SyncDirectory)
{
  std::filesystem::create_directories(directory);
  const auto canonical = std::filesystem::canonical(directory);
  for (auto current = canonical;; current = current.parent_path()) {
    sync(current);
    if (current == current.root_path()) {
      break;
    }
  }
  return canonical;
}
}
