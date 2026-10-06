#include "FormDesc.h"
#include <cstdio>
#include <stdexcept>

std::string FormDesc::ToString(char delimiter) const
{
  auto fullFmt = "%0x%c%s";
  auto idFmt = "%0x";
  size_t size = !file.empty()
    ? std::snprintf(nullptr, 0, fullFmt, shortFormId, delimiter, file.c_str())
    : std::snprintf(nullptr, 0, idFmt, shortFormId);

  std::string buffer;
  buffer.resize(size + 1);

  if (!file.empty()) {
    std::sprintf(buffer.data(), fullFmt, shortFormId, delimiter, file.c_str());
  } else {
    std::sprintf(buffer.data(), idFmt, shortFormId);
  }
  buffer.resize(size); // remove extra null terminator
  return buffer;
}

FormDesc FormDesc::FromString(const std::string& str, char delimiter)
{
  FormDesc res;
  std::string id, file;

  if (str.find(delimiter) == std::string::npos) {
    std::sscanf(str.data(), "%x", &res.shortFormId);
    return res;
  }

  for (auto it = str.begin(); it != str.end(); ++it) {
    if (*it == delimiter) {
      id = { str.begin(), it };
      res.file = { it + 1, str.end() };
      break;
    }
  }

  std::sscanf(id.data(), "%x", &res.shortFormId);
  return res;
}

namespace {
std::string Hex(uint32_t value)
{
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%x", value);
  return buffer;
}
}

uint32_t FormDesc::ToFormId(const espm::LoadOrder& loadOrder) const
{
  // Workaround legacy tests throwing exceptions (drop support for PartOne
  // instances without espm to remove this)
  static const std::string kSkyrimEsm = "Skyrim.esm";
  if (shortFormId == 0x3c && file == kSkyrimEsm) {
    return 0x3c;
  }

  if (file.empty()) {
    return 0xff000000 + shortFormId;
  }

  const auto fileIdx = loadOrder.FindByFileName(file);
  if (!fileIdx) {
    throw std::runtime_error(file + " not found in loaded files");
  }

  // Thornswood #1715. A light plugin's form is not fileIdx << 24: the game
  // gives it 0xFE000000 | (lightIndex << 12) | id, and the id has 12 bits.
  // An id wider than its plugin allows names no form of that plugin (it used
  // to run on into the next plugin's ids), so it is refused rather than cut
  // down into some other form.
  const auto slot = loadOrder.GetSlot(*fileIdx);
  if ((shortFormId & ~slot.LocalIdMask()) != 0) {
    throw std::runtime_error(
      ToString() + " names no form: " + file + " is a " +
      (slot.light ? "light" : "full") + " plugin, whose form ids go up to " +
      Hex(slot.LocalIdMask()));
  }
  return slot.ToId(shortFormId);
}

FormDesc FormDesc::FromFormId(uint32_t formId,
                              const espm::LoadOrder& loadOrder)
{
  // Workaround legacy tests throwing exceptions (drop support for PartOne
  // instances without espm to remove this)
  if (formId == 0x3c) {
    return FormDesc::Tamriel();
  }

  FormDesc res;
  if (formId >= 0xff000000) {
    res.shortFormId = formId - 0xff000000;
    return res;
  }

  // Thornswood #1715. formId >> 24 is a plugin's place in the load order only
  // while every plugin is full. Every light plugin's form has 0xFE there, so
  // every one of them came back as "invalid file index 254", among them the
  // two beard head parts WWG Ghost's character wore on 5 Oct, 0xFE029827 and
  // 0xFE029838 from KhisartinBeards.esp.
  const auto fileIdx = loadOrder.FindByFormId(formId);
  if (!fileIdx) {
    const auto slot = espm::PluginSlot::Of(formId);
    throw std::runtime_error("FromFormId failed due to invalid file index " +
                             std::string(slot->light ? "light " : "") +
                             std::to_string(slot->index) + " (" +
                             Hex(formId) + ")");
  }
  res.file = loadOrder[*fileIdx];
  res.shortFormId = formId & loadOrder.GetSlot(*fileIdx).LocalIdMask();
  return res;
}

static const FormDesc kTamriel = FormDesc::FromString("3c:Skyrim.esm");

FormDesc FormDesc::Tamriel()
{
  return kTamriel;
}
