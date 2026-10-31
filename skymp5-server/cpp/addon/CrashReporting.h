#pragma once
// A native crash of the server is recorded by Windows (Thornswood #1267).
//
// The dev server died three times on 29 September with 0xC0000005 (an access
// violation) and nothing recorded where: no Application Error (1000) or WER
// (1001) event for node.exe and nothing in WER's report archive. LocalDumps
// is armed for node.exe on the box now (C:\Thornswood-Dumps, full dumps,
// read 21 October) and could not have caught one either. The cause is in the
// process, not on the box: libuv's uv__init, which node runs at startup
// before any addon loads, calls
//
//   SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
//                SEM_NOOPENFILEERRORBOX);
//
// (deps/uv/src/win/core.c in node v24.21.0, the version the box runs), and
// for SEM_NOGPFAULTERRORBOX Microsoft's SetErrorMode page says "The system
// does not invoke Windows Error Reporting". So every native crash of node on
// Windows ends the process with its exception code as the exit code and
// nothing else: no event, no LocalDumps dump, whatever the registry says.
// libuv sets it so a crash never stops on a dialog; the same page names the
// way to have no dialog without losing the report: WerSetFlags with
// WER_FAULT_REPORTING_NO_UI.
//
// So when node loads this addon, Allow sets WER_FAULT_REPORTING_NO_UI and
// clears SEM_NOGPFAULTERRORBOX (and only it: a missing disk or file still
// gives no dialog). A crash is then reported by Windows the way it
// is for any other program: the Application log gets its Application Error
// event (the faulting module and offset), and LocalDumps, where it is set for
// node.exe, writes its dump. misc/tests/crash_reporting_test.cpp crashes a
// process both ways with LocalDumps armed.

#include <string>

namespace CrashReporting {

// Has Windows report a crash of this process, with no dialog. Returns what
// failed, or an empty string. Elsewhere there is nothing to change.
std::string Allow();

}
