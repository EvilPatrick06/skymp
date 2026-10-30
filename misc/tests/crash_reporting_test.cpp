// CrashReporting::Allow, which scam_native.node calls as node loads it
// (Thornswood #1267), against Windows itself: a process that crashes with
// the error mode node's libuv gives every node process leaves no dump even
// with LocalDumps armed for it, which is the box on 21 October, and the same
// crash after Allow leaves one.
//
//   crash-reporting-test <scratch folder>
//
// The crashes are this program run again with crash-as-node or
// crash-as-addon. Arming LocalDumps writes HKLM, which needs administrator:
// on GitHub's runners (CI set) it must work, elsewhere the crashes are
// skipped, saying so. The key it writes is this program's own and is removed
// afterwards.
#include "CrashReporting.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <werapi.h>
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

#ifdef _WIN32
// What libuv's uv__init sets in every node process (deps/uv/src/win/core.c,
// node v24.21.0).
constexpr UINT kLibuvErrorMode =
  SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;

const wchar_t* kLocalDumps =
  L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\"
  L"crash-reporting-test.exe";

[[noreturn]] void Crash()
{
  *static_cast<volatile int*>(nullptr) = 1;
  std::abort();
}

int Dumps(const fs::path& dir)
{
  int n = 0;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".dmp") {
      ++n;
    }
  }
  return n;
}

// Runs this program again as `how`, with LocalDumps writing into `dir`, and
// says what it ended with and how many dumps it left.
void CrashOnce(const std::wstring& how, const fs::path& dir, DWORD& exitCode,
               int& dumps)
{
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  HKEY key = nullptr;
  RegCreateKeyExW(HKEY_LOCAL_MACHINE, kLocalDumps, 0, nullptr, 0,
                  KEY_SET_VALUE, nullptr, &key, nullptr);
  std::wstring folder = dir.wstring();
  RegSetValueExW(key, L"DumpFolder", 0, REG_EXPAND_SZ,
                 reinterpret_cast<const BYTE*>(folder.c_str()),
                 static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t)));
  DWORD type = 1, count = 2;
  RegSetValueExW(key, L"DumpType", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&type), sizeof(type));
  RegSetValueExW(key, L"DumpCount", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&count), sizeof(count));
  RegCloseKey(key);

  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring line = L"\"" + std::wstring(self) + L"\" " + how;
  STARTUPINFOW si{ sizeof(si) };
  PROCESS_INFORMATION pi{};
  exitCode = 0;
  if (CreateProcessW(self, line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                     nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  dumps = Dumps(dir);
}
#endif

}

int main(int argc, char** argv)
{
#ifdef _WIN32
  std::string arg = argc > 1 ? argv[1] : "";
  if (arg == "crash-as-node") {
    SetErrorMode(kLibuvErrorMode);
    Crash();
  }
  if (arg == "crash-as-addon") {
    SetErrorMode(kLibuvErrorMode);
    CrashReporting::Allow();
    Crash();
  }
  if (argc < 2) {
    std::cout << "usage: crash-reporting-test <scratch folder>\n";
    return 2;
  }

  // Allow itself, in this process.
  SetErrorMode(kLibuvErrorMode);
  std::string error = CrashReporting::Allow();
  Check(error.empty(),
        "Allow says nothing failed" +
          (error.empty() ? std::string() : ": " + error));
  UINT mode = GetErrorMode();
  Check(!(mode & SEM_NOGPFAULTERRORBOX),
        "after Allow, Windows Error Reporting is invoked for a crash "
        "(SEM_NOGPFAULTERRORBOX is clear)");
  Check((mode & SEM_FAILCRITICALERRORS) && (mode & SEM_NOOPENFILEERRORBOX),
        "the rest of libuv's error mode is kept (no dialog for a missing "
        "disk or file)");
  DWORD flags = 0;
  Check(SUCCEEDED(WerGetFlags(GetCurrentProcess(), &flags)) &&
          (flags & WER_FAULT_REPORTING_NO_UI),
        "and it reports with no dialog (WER_FAULT_REPORTING_NO_UI)");

  // A real crash both ways, with LocalDumps armed the way the box has it for
  // node.exe.
  HKEY key = nullptr;
  LSTATUS created =
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, kLocalDumps, 0, nullptr, 0,
                    KEY_SET_VALUE, nullptr, &key, nullptr);
  if (created != ERROR_SUCCESS) {
    bool ci = GetEnvironmentVariableA("CI", nullptr, 0) > 0;
    std::cout << (ci ? "FAIL  " : "SKIP  ")
              << "the crashes: arming LocalDumps for this program needs "
                 "administrator (RegCreateKeyEx said "
              << created << ")\n";
    if (ci) {
      ++failed;
    }
  } else {
    RegCloseKey(key);
    fs::path root = fs::absolute(argv[1]);

    DWORD code = 0;
    int dumps = 0;
    CrashOnce(L"crash-as-node", root / "as-node", code, dumps);
    Check(code == EXCEPTION_ACCESS_VIOLATION,
          "with libuv's error mode alone the crash ends the process with "
          "0xC0000005, as the dev server's task did (exit code " +
            std::to_string(code) + ")");
    Check(dumps == 0,
          "and leaves no dump although LocalDumps is armed, as on the box (" +
            std::to_string(dumps) + " dumps)");

    CrashOnce(L"crash-as-addon", root / "as-addon", code, dumps);
    Check(code == EXCEPTION_ACCESS_VIOLATION,
          "after Allow the same crash ends it with 0xC0000005 (exit code " +
            std::to_string(code) + ")");
    Check(dumps == 1,
          "and Windows writes its dump (" + std::to_string(dumps) + " dumps)");

    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kLocalDumps, 0, 0);
  }

  std::cout << (failed ? "FAILED\n" : "ALL PASSED\n");
  return failed ? 1 : 0;
#else
  (void)argc;
  (void)argv;
  Check(CrashReporting::Allow().empty(),
        "Allow changes nothing where there is no Windows Error Reporting");
  std::cout << "SKIP  the crashes: Windows Error Reporting is Windows only\n";
  std::cout << (failed ? "FAILED\n" : "ALL PASSED\n");
  return failed ? 1 : 0;
#endif
}
