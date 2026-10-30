#pragma once

#include "NapiHelper.h"

namespace ObjectReferenceApi {

Napi::Value SetCollision(const Napi::CallbackInfo& info);
Napi::Value GetWorkbenchType(const Napi::CallbackInfo& info);

inline void Register(Napi::Env env, Napi::Object& exports)
{
  exports.Set(
    "setCollision",
    Napi::Function::New(env, NapiHelper::WrapCppExceptions(SetCollision)));
  exports.Set(
    "getWorkbenchType",
    Napi::Function::New(env, NapiHelper::WrapCppExceptions(GetWorkbenchType)));
}
}
