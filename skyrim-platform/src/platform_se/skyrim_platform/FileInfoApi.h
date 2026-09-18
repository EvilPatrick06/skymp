#pragma once

#include "NapiHelper.h"

namespace FileInfoApi {

Napi::Value FileInfo(const Napi::CallbackInfo& info);

inline void Register(Napi::Env env, Napi::Object& exports)
{
  // FileInfo throws InvalidArgumentException, which derives from
  // std::runtime_error and NOT from Napi::Error. node-addon-api only converts
  // Napi::Error unless NODE_ADDON_API_CPP_EXCEPTIONS_ALL is defined, and it is
  // not defined anywhere in this tree, so the raw C++ exception used to unwind
  // straight through V8's frames. That is undefined behaviour, and it is also
  // why the try/catch around getFileInfo in loadOrderVerificationService could
  // never fire. WrapCppExceptions was written for exactly this and had no call
  // sites until now.
  exports.Set("getFileInfo",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(FileInfo)));
}

}
