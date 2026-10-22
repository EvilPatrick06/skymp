#include "CrashReporting.h"

#ifdef _WIN32
#  include <cstdio>

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <werapi.h>
#endif

std::string CrashReporting::Allow()
{
#ifdef _WIN32
  SetErrorMode(GetErrorMode() & ~SEM_NOGPFAULTERRORBOX);
  HRESULT hr = WerSetFlags(WER_FAULT_REPORTING_NO_UI);
  if (FAILED(hr)) {
    char text[96];
    std::snprintf(text, sizeof(text),
                  "WerSetFlags(WER_FAULT_REPORTING_NO_UI) failed: 0x%08lX",
                  static_cast<unsigned long>(hr));
    return text;
  }
#endif
  return {};
}
