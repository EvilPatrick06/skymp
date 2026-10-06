#pragma once
// Node values and fmt are supplied by this fixture. The face builder and the
// helpers it calls are extracted unchanged from LoadGameApi.cpp by
// test_light_face_refs.ps1.
#include "savefile/SFChangeFormNPC.h"
#include "savefile/SFStructure.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
namespace Napi {
class JSValue {
  nlohmann::json data;
  bool defined = false;
public:
  JSValue() = default;
  JSValue(nlohmann::json data) : data(std::move(data)), defined(true) {}
  bool IsUndefined() const { return !defined; }
  bool IsNull() const { return defined && data.is_null(); }
  bool IsNumber() const { return defined && data.is_number(); }
  bool IsString() const { return defined && data.is_string(); }
  bool IsObject() const { return defined && data.is_object(); }
  bool IsArray() const { return defined && data.is_array(); }
  // A Napi value converts to a non-null handle, so it reads as true.
  explicit operator bool() const { return defined; }
  struct String {
    std::string text;
    operator std::string() const { return text; }
  };
  String ToString() const {
    return { data.is_string() ? data.get<std::string>() : data.dump() };
  }
  double DoubleValue() const { return data.get<double>(); }
  uint32_t Length() const { return static_cast<uint32_t>(data.size()); }
  JSValue Get(const char* key) const {
    return data.contains(key) ? JSValue(data.at(key)) : JSValue();
  }
  JSValue Get(uint32_t index) const { return JSValue(data.at(index)); }
};
using Value = JSValue;
using Object = JSValue;
using Array = JSValue;
}
namespace NapiHelper {
inline Napi::Object ExtractObject(Napi::Value value, const char*) {
  if (!value.IsObject()) throw std::runtime_error("Expected object");
  return value;
}
inline Napi::Array ExtractArray(Napi::Value value, const char*) {
  if (!value.IsArray()) throw std::runtime_error("Expected array");
  return value;
}
inline std::string ExtractString(Napi::Value value, const char*) {
  if (!value.IsString()) throw std::runtime_error("Expected string");
  return value.ToString();
}
// Napi's Uint32Value and Int32Value are ECMAScript ToUint32 and ToInt32.
inline uint32_t ExtractUInt32(Napi::Value value, const char*) {
  if (!value.IsNumber()) throw std::runtime_error("Expected number");
  return static_cast<uint32_t>(static_cast<int64_t>(value.DoubleValue()));
}
inline int32_t ExtractInt32(Napi::Value value, const char*) {
  return static_cast<int32_t>(ExtractUInt32(value, ""));
}
}
namespace fmt {
template <class T>
std::string format(const char* pattern, T value) {
  std::string text(pattern);
  auto at = text.find("{}");
  if (at != std::string::npos) text.replace(at, 2, std::to_string(value));
  return text;
}
}
