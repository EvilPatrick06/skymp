#include "CrashReporting.h"
#include "ScampServer.h"

#include <iostream>

#ifndef NAPI_CPP_EXCEPTIONS
#  error NAPI_CPP_EXCEPTIONS must be defined or throwing from JS code would crash!
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  // Before anything else of the server's runs, so a native crash anywhere in
  // it is reported by Windows (CrashReporting.h, Thornswood #1267).
  std::string crashReporting = CrashReporting::Allow();
  if (!crashReporting.empty()) {
    std::cerr << "A native crash of this server will not be recorded: "
              << crashReporting << std::endl;
  }
  return ScampServer::Init(env, exports);
}

NODE_API_MODULE(scamp, Init)
