#pragma once
#include "libespm/LoadOrder.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

class FormDesc
{
public:
  FormDesc() = default;
  FormDesc(uint32_t shortFormId_, std::string file_)
    : shortFormId(shortFormId_)
    , file(file_)
  {
  }

  std::string ToString(char delimiter = ':') const;
  static FormDesc FromString(const std::string& str, char delimiter = ':');

  // "id:file" to the id the game gives that form, and back. A light
  // plugin's forms come out under 0xFE (espm::PluginSlot); the id part of
  // their descriptor is the 12 bit id inside the plugin, as in
  // "827:KhisartinBeards.esp" for 0xFE029827.
  uint32_t ToFormId(const espm::LoadOrder& loadOrder) const;
  static FormDesc FromFormId(uint32_t formId,
                             const espm::LoadOrder& loadOrder);

  friend bool operator==(const FormDesc& left, const FormDesc& right)
  {
    return std::make_tuple(left.shortFormId, left.file) ==
      std::make_tuple(right.shortFormId, right.file);
  }

  friend bool operator!=(const FormDesc& left, const FormDesc& right)
  {
    return !(left == right);
  }

  friend bool operator<(const FormDesc& left, const FormDesc& right)
  {
    return std::make_tuple(left.shortFormId, left.file) <
      std::make_tuple(right.shortFormId, right.file);
  }

  static FormDesc Tamriel();

  uint32_t shortFormId = 0;
  std::string file;
};
