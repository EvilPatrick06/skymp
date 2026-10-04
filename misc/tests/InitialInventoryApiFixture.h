#pragma once
// Engine and Node values are supplied by this fixture. The inventory builder
// and numeric validator below are extracted unchanged from LoadGameApi.cpp.
#include <nlohmann/json.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <limits>
namespace Napi {
class JSValue {
  nlohmann::json data;
  bool defined = false;
public:
  JSValue() = default;
  JSValue(nlohmann::json data) : data(std::move(data)), defined(true) {}
  bool IsUndefined() const { return !defined; }
  bool IsNumber() const { return defined && data.is_number(); }
  bool IsBoolean() const { return defined && data.is_boolean(); }
  bool IsObject() const { return defined && data.is_object(); }
  bool IsArray() const { return defined && data.is_array(); }
  template<class T> T As() const { return *this; }
  double DoubleValue() const { return data.get<double>(); }
  JSValue ToBoolean() const { return *this; }
  bool Value() const { return data.get<bool>(); }
  JSValue ToString() const { return *this; }
  std::string Utf8Value() const { return data.get<std::string>(); }
  uint32_t Length() const { return static_cast<uint32_t>(data.size()); }
  JSValue Get(const char* key) const { return data.contains(key) ? JSValue(data.at(key)) : JSValue(); }
  JSValue Get(uint32_t index) const { return JSValue(data.at(index)); }
  JSValue GetPropertyNames() const {
    auto keys = nlohmann::json::array();
    for (auto it = data.begin(); it != data.end(); ++it) keys.push_back(it.key());
    return JSValue(keys);
  }
};
using Value = JSValue;
using Object = JSValue;
using Array = JSValue;
using Number = JSValue;
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
}
namespace RE {
struct TESContainer;
struct TESForm {
  uint32_t formID;
  TESContainer* container = nullptr;
  static inline std::map<uint32_t, TESForm> forms;
  static TESForm* LookupByID(uint32_t id) {
    auto it = forms.find(id); return it == forms.end() ? nullptr : &it->second;
  }
  template<class T> T* As() { return container; }
};
struct ContainerObject { TESForm* obj; int32_t count; };
struct TESContainer {
  uint32_t numContainerObjects = 0;
  std::vector<ContainerObject*> containerObjects;
};
}
