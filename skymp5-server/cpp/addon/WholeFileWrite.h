#pragma once
// A whole-file write for files other programs read while the server runs
// (Thornswood #2740).
//
// The gamemode publishes small files that other programs read while it runs:
// data/online.json, which the resources port serves to every launcher and
// game and which the site reads from disk, and private files the deploy and
// staff scripts read. Node can only write them in place, and in place a
// reader sees an empty or cut-short file for about a millisecond of every
// write (measured on #2740: 43 of 810 site-style reads empty and 70 of 782
// HTTP answers bad with a write every 20 ms). Node cannot replace the file
// whole either: fs.renameSync on Windows is MoveFileEx(REPLACE_EXISTING),
// which Windows refuses while any handle is open on the file it replaces,
// share-delete or not (1481 of 1500 writes refused beside node readers), and
// Node exposes no other rename.
//
// So this writes the bytes to a new file beside the target, flushes them, and
// renames that file over the target with POSIX semantics: on Windows
// SetFileInformationByHandle with FileRenameInfoEx and
// FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS, the
// rename Thornswood's feed store measured on #2727 (2000 swaps beside node
// readers, none refused); on other systems rename(2), which has those
// semantics already. A reader that opened the old file keeps reading the old
// bytes, and any open after the rename gets the new file whole. A reader that
// opened the target WITHOUT share-delete still refuses the rename, and then
// this throws, saying so; and one that opens without share-delete in the
// moment between the rename and the renaming handle closing is refused its
// open. Node opens every file with share-delete; the scripts that read these
// files on the box are written to. Needs NTFS on Windows 10 1709 or Windows Server 2019
// or later; the servers run Windows Server 2022.
//
// A failure throws with what failed and why, and the target is left as it
// was: the new file is removed. There is no fallback to an in-place write and
// no second try; the caller decides what a failed write means.
//
// The new file is named after the target with a dot in front
// (.online.json.whole-<pid>-<n>), so the resources port, which serves nothing
// whose path has a part starting with a dot, never serves it.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace WholeFileWrite {

namespace Detail {

// A path as UTF-8 in a std::string, under C++17 (where u8string() is a
// std::string) and C++20 (where it is a std::u8string) alike.
inline std::string Utf8(const std::filesystem::path& p)
{
  const auto s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

inline std::runtime_error Failed(const std::filesystem::path& target,
                                 const std::string& step, int code)
{
  return std::runtime_error(
    "Could not write " + Utf8(target) + " whole: " + step + ": " +
    std::system_category().message(code) + " (" + std::to_string(code) +
    "). " + Utf8(target.filename()) + " is as it was.");
}

inline std::filesystem::path Beside(const std::filesystem::path& target)
{
  static std::atomic<unsigned long long> counter{ 0 };
#ifdef _WIN32
  const unsigned long pid = GetCurrentProcessId();
#else
  const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
  std::filesystem::path name(".");
  name += target.filename();
  name += ".whole-" + std::to_string(pid) + "-" + std::to_string(counter++);
  return target.parent_path() / name;
}

}

// Replaces the file at target with exactly these bytes, whole. Throws
// std::runtime_error when it cannot, and then target is unchanged.
inline void Write(const std::filesystem::path& target, std::string_view bytes)
{
  if (target.empty() || !target.has_filename()) {
    throw std::runtime_error("Could not write '" + Detail::Utf8(target) +
                             "' whole: it names no file");
  }
  const auto absolute = std::filesystem::absolute(target);
  const auto temporary = Detail::Beside(absolute);

#ifdef _WIN32
  // Removes the new file through a handle that has DELETE, when the write
  // does not end with it in place. The target was never touched.
  const auto removeThrough = [](HANDLE h) {
    FILE_DISPOSITION_INFO gone{};
    gone.DeleteFile = TRUE;
    SetFileInformationByHandle(h, FileDispositionInfo, &gone, sizeof(gone));
  };

  // The bytes, through a handle that only writes, shared with nobody: the
  // file has a dot name nobody else opens until it is renamed.
  {
    HANDLE w = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (w == INVALID_HANDLE_VALUE) {
      throw Detail::Failed(target,
                           "creating " + Detail::Utf8(temporary.filename()),
                           static_cast<int>(GetLastError()));
    }
    bool written = false;
    struct Owned
    {
      HANDLE h;
      bool& written;
      void (*remove)(HANDLE);
      ~Owned()
      {
        if (!written) {
          remove(h);
        }
        CloseHandle(h);
      }
    } owned{ w, written, removeThrough };

    size_t done = 0;
    while (done < bytes.size()) {
      const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 30));
      DWORD wrote = 0;
      if (!WriteFile(w, bytes.data() + done, chunk, &wrote, nullptr)) {
        throw Detail::Failed(target, "writing the new file",
                             static_cast<int>(GetLastError()));
      }
      if (wrote == 0) {
        throw Detail::Failed(target, "writing the new file",
                             ERROR_WRITE_FAULT);
      }
      done += wrote;
    }
    if (!FlushFileBuffers(w)) {
      throw Detail::Failed(target, "flushing the new file",
                           static_cast<int>(GetLastError()));
    }
    written = true;
  }

  // The rename, through a handle with no read or write access and shared all
  // ways, the one Thornswood's feed store renames through (#2727): DELETE |
  // SYNCHRONIZE | FILE_READ_ATTRIBUTES. Not the writing handle: from the
  // rename until a handle closes, the target's name is that handle's file,
  // and a reader opening it then must share every access that handle has.
  // Measured on #2740 with the writing handle, shared with nobody: every
  // reader's open in that moment was refused (EBUSY), 11 of 1078 site-style
  // reads. Through this one a reader that shares delete, as node does, opens
  // the new file.
  HANDLE h = CreateFileW(temporary.c_str(),
                         DELETE | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    DeleteFileW(temporary.c_str());
    throw Detail::Failed(target, "opening the new file to rename it",
                         static_cast<int>(error));
  }
  bool placed = false;
  struct Renaming
  {
    HANDLE h;
    bool& placed;
    void (*remove)(HANDLE);
    ~Renaming()
    {
      if (!placed) {
        remove(h);
      }
      CloseHandle(h);
    }
  } renaming{ h, placed, removeThrough };

  // FILE_RENAME_INFO, written by offset rather than through the SDK's union so
  // it does not depend on which Windows version the build targets: Flags at
  // 0, RootDirectory (none, so FileName is the whole path), FileNameLength in
  // bytes, then the name and its terminator.
  constexpr auto kFileRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
  constexpr DWORD kReplaceIfExists = 0x1;  // FILE_RENAME_FLAG_REPLACE_IF_EXISTS
  constexpr DWORD kPosixSemantics = 0x2;   // FILE_RENAME_FLAG_POSIX_SEMANTICS
  const std::wstring& name = absolute.native();
  const size_t nameAt = offsetof(FILE_RENAME_INFO, FileName);
  std::vector<std::uint8_t> info(nameAt + (name.size() + 1) * sizeof(wchar_t),
                                 0);
  const DWORD flags = kReplaceIfExists | kPosixSemantics;
  std::memcpy(info.data(), &flags, sizeof(flags));
  auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(info.data());
  rename->RootDirectory = nullptr;
  rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
  std::memcpy(info.data() + nameAt, name.c_str(),
              (name.size() + 1) * sizeof(wchar_t));
  if (!SetFileInformationByHandle(h, kFileRenameInfoEx, info.data(),
                                  static_cast<DWORD>(info.size()))) {
    const DWORD error = GetLastError();
    throw Detail::Failed(
      target,
      error == ERROR_SHARING_VIOLATION
        ? "renaming the new file over it, refused because another program "
          "has it open without sharing delete"
        : "renaming the new file over it",
      static_cast<int>(error));
  }
  placed = true;
#else
  const int fd = open(temporary.c_str(),
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                      0644);
  if (fd < 0) {
    throw Detail::Failed(target, "creating " + Detail::Utf8(temporary.filename()),
                         errno);
  }
  struct Owned
  {
    int fd;
    std::filesystem::path path;
    bool placed = false;
    ~Owned()
    {
      if (fd >= 0) {
        close(fd);
      }
      if (!placed) {
        unlink(path.c_str());
      }
    }
  } owned{ fd, temporary, false };

  size_t done = 0;
  while (done < bytes.size()) {
    const ssize_t wrote = write(fd, bytes.data() + done, bytes.size() - done);
    if (wrote < 0 && errno == EINTR) {
      continue;  // a signal, not a failure: the write is simply resumed
    }
    if (wrote <= 0) {
      throw Detail::Failed(target, "writing the new file",
                           wrote < 0 ? errno : EIO);
    }
    done += static_cast<size_t>(wrote);
  }
  if (fsync(fd) != 0) {
    throw Detail::Failed(target, "flushing the new file", errno);
  }
  const int closed = close(owned.fd);
  owned.fd = -1;
  if (closed != 0) {
    throw Detail::Failed(target, "closing the new file", errno);
  }
  if (rename(temporary.c_str(), absolute.c_str()) != 0) {
    throw Detail::Failed(target, "renaming the new file over it", errno);
  }
  owned.placed = true;
#endif
}

}
