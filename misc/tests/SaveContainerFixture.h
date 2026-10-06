#pragma once
// A reading of the Skyrim save container that does not use savefile, so the
// tests can check savefile's Reader and Writer against something other than
// themselves. It follows two published readers that are used on real saves:
// Wrye Bash (Mopy/bash/bosh/save_headers.py: SkyrimSaveHeader,
// _AEslSaveHeader) and FallrimTools ReSaver (resaver/ess/ESS.java,
// Header.java, PluginInfo.java). Both read:
//   magic, uint32 header size, the header, which from save version 12
//   (Special Edition) ends with a uint16 compression type (0 none, 1 zlib,
//   2 LZ4 block) and is counted in the header size;
//   the screenshot, 4 bytes a pixel from version 12, 3 before;
//   uint32 uncompressed and compressed lengths when the compression type is
//   not 0, then the compressed body;
//   the body: uint8 form version, uint32 plugin info size, uint8 plugin count
//   and names, and from form version 78 a uint16 light plugin count and names,
//   all counted in the plugin info size; then the file location table, whose
//   offsets count from the start of the file as if the body followed the
//   screenshot uncompressed (ReSaver subtracts the header end from them to
//   index the decompressed body).
// A light plugin's form is 0xFE000000 | light index << 12 | local id, the
// light index naming an entry of the save's light plugin list
// (ReSaver PluginInfo.splitFormID and makeFormID).
#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace SaveContainerFixture {

inline std::vector<uint8_t> ReadFile(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

inline void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (!out) throw std::runtime_error("cannot write " + path);
}

inline uint32_t U32(const std::vector<uint8_t>& b, size_t at)
{
  if (at + 4 > b.size()) throw std::runtime_error("read past the end");
  uint32_t v;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}

inline uint16_t U16(const std::vector<uint8_t>& b, size_t at)
{
  if (at + 2 > b.size()) throw std::runtime_error("read past the end");
  uint16_t v;
  std::memcpy(&v, b.data() + at, 2);
  return v;
}

inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
{
  b.insert(b.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
}

inline void PutU16(std::vector<uint8_t>& b, uint16_t v)
{
  b.insert(b.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2);
}

inline std::string WString(const std::vector<uint8_t>& b, size_t& at)
{
  const uint16_t n = U16(b, at);
  at += 2;
  if (at + n > b.size()) throw std::runtime_error("string past the end");
  std::string s(b.begin() + at, b.begin() + at + n);
  at += n;
  return s;
}

inline void PutWString(std::vector<uint8_t>& b, const std::string& s)
{
  PutU16(b, static_cast<uint16_t>(s.size()));
  b.insert(b.end(), s.begin(), s.end());
}

struct Container
{
  uint32_t headerSize = 0;
  uint32_t version = 0;
  uint16_t compression = 0;
  uint32_t shotWidth = 0, shotHeight = 0;
  size_t headerEnd = 0; // first byte after the screenshot
  // The file with its body decompressed in place, so that the file
  // location table's offsets index it directly.
  std::vector<uint8_t> flat;
  uint8_t formVersion = 0;
  uint32_t pluginInfoSize = 0;
  size_t pluginInfoRead = 0; // bytes from the plugin count to the end of the lists
  std::vector<std::string> plugins, lightPlugins;
  bool hasLightList = false;
  uint32_t flt[10] = {};
  size_t fltAt = 0;
};

inline std::vector<uint8_t> Inflate(const uint8_t* src, size_t n, size_t outSize)
{
  std::vector<uint8_t> out(outSize);
  uLongf len = static_cast<uLongf>(outSize);
  if (uncompress(out.data(), &len, src, static_cast<uLong>(n)) != Z_OK || len != outSize)
    throw std::runtime_error("zlib body did not inflate to its stated length");
  return out;
}

// LZ4 block format: token (literal length high nibble, match length - 4 low
// nibble, 15 meaning more bytes follow), literals, 2-byte offset, match.
// Kept separate from savefile's decoder on purpose.
inline std::vector<uint8_t> Lz4Expand(const uint8_t* src, size_t n, size_t outSize)
{
  std::vector<uint8_t> out;
  out.reserve(outSize);
  size_t i = 0;
  auto more = [&](size_t len) {
    if (len != 15) return len;
    for (;;) {
      if (i >= n) throw std::runtime_error("lz4 length past the end");
      const uint8_t b = src[i++];
      len += b;
      if (b != 255) return len;
    }
  };
  while (i < n) {
    const uint8_t token = src[i++];
    const size_t lit = more(token >> 4);
    if (i + lit > n) throw std::runtime_error("lz4 literals past the end");
    out.insert(out.end(), src + i, src + i + lit);
    i += lit;
    if (i == n) break;
    if (i + 2 > n) throw std::runtime_error("lz4 offset past the end");
    const size_t offset = src[i] | (size_t(src[i + 1]) << 8);
    i += 2;
    const size_t len = more(token & 15) + 4;
    if (offset == 0 || offset > out.size()) throw std::runtime_error("lz4 offset out of range");
    for (size_t k = 0, from = out.size() - offset; k < len; ++k) out.push_back(out[from + k]);
  }
  if (out.size() != outSize) throw std::runtime_error("lz4 body did not expand to its stated length");
  return out;
}

// An LZ4 block made of one literal run is a valid block (lz4
// doc/lz4_Block_format.md: the last sequence holds only literals).
inline std::vector<uint8_t> Lz4Literals(const std::vector<uint8_t>& data)
{
  std::vector<uint8_t> out;
  size_t len = data.size();
  out.push_back(static_cast<uint8_t>(len < 15 ? len << 4 : 0xF0));
  if (len >= 15) {
    len -= 15;
    while (len >= 255) {
      out.push_back(255);
      len -= 255;
    }
    out.push_back(static_cast<uint8_t>(len));
  }
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

inline Container Parse(const std::vector<uint8_t>& file)
{
  Container c;
  if (file.size() < 17 || std::memcmp(file.data(), "TESV_SAVEGAME", 13) != 0)
    throw std::runtime_error("not a Skyrim save");
  c.headerSize = U32(file, 13);
  size_t at = 17;
  c.version = U32(file, at);
  at += 8; // version, save number
  WString(file, at); // name
  at += 4;           // level
  WString(file, at); // location
  WString(file, at); // game date
  WString(file, at); // race editor id
  at += 2 + 4 + 4 + 8; // sex, current xp, level-up xp, filetime
  c.shotWidth = U32(file, at);
  c.shotHeight = U32(file, at + 4);
  at += 8;
  const bool se = c.version >= 12;
  if (se) {
    c.compression = U16(file, at);
    at += 2;
  }
  if (at - 17 != c.headerSize) throw std::runtime_error("header size does not match the header");
  at += size_t(c.shotWidth) * c.shotHeight * (se ? 4 : 3);
  c.headerEnd = at;
  c.flat.assign(file.begin(), file.begin() + at);
  if (c.compression == 0) {
    c.flat.insert(c.flat.end(), file.begin() + at, file.end());
  } else {
    const uint32_t uncompressed = U32(file, at), compressed = U32(file, at + 4);
    if (at + 8 + compressed != file.size()) throw std::runtime_error("compressed length does not reach the end of the file");
    const uint8_t* src = file.data() + at + 8;
    auto body = c.compression == 1 ? Inflate(src, compressed, uncompressed)
                                   : c.compression == 2 ? Lz4Expand(src, compressed, uncompressed)
                                                        : throw std::runtime_error("unknown compression type");
    c.flat.insert(c.flat.end(), body.begin(), body.end());
  }
  at = c.headerEnd;
  c.formVersion = c.flat.at(at++);
  c.pluginInfoSize = U32(c.flat, at);
  at += 4;
  const size_t listStart = at;
  const uint8_t count = c.flat.at(at++);
  for (int i = 0; i < count; ++i) c.plugins.push_back(WString(c.flat, at));
  c.hasLightList = se && c.formVersion >= 78;
  if (c.hasLightList) {
    const uint16_t light = U16(c.flat, at);
    at += 2;
    for (int i = 0; i < light; ++i) c.lightPlugins.push_back(WString(c.flat, at));
  }
  c.pluginInfoRead = at - listStart;
  c.fltAt = at;
  for (auto& v : c.flt) {
    v = U32(c.flat, at);
    at += 4;
  }
  return c;
}

// The plugin a form id array entry names in this save, or "" if it names
// none. Created forms (0xFF) belong to the save itself.
inline std::string PluginOf(const Container& c, uint32_t formId)
{
  const uint32_t top = formId >> 24;
  if (top == 0xFF) return "(created)";
  if (top == 0xFE) {
    if (!c.hasLightList) return "";
    const uint32_t index = (formId >> 12) & 0xFFF;
    return index < c.lightPlugins.size() ? c.lightPlugins[index] : "";
  }
  return top < c.plugins.size() ? c.plugins[top] : "";
}

inline std::vector<uint32_t> FormIdArray(const Container& c)
{
  const size_t at = c.flt[0];
  const uint32_t n = U32(c.flat, at);
  std::vector<uint32_t> ids(n);
  for (uint32_t i = 0; i < n; ++i) ids[i] = U32(c.flat, at + 4 + 4 * size_t(i));
  return ids;
}

// Checks that each table offset lands where the body says the table is:
// the global data tables start with their first type, the change forms with
// the first record, the form id array with its count and the unknown table
// with its size. Returns "" or what is wrong.
inline std::string CheckOffsets(const Container& c)
{
  const size_t fltEnd = c.fltAt + 4 * 25;
  if (c.flt[2] != fltEnd) return "global data table 1 does not follow the file location table";
  if (U32(c.flat, c.flt[2]) != 0) return "global data table 1 does not start with type 0";
  if (U32(c.flat, c.flt[3]) != 100) return "global data table 2 does not start with type 100";
  if (U32(c.flat, c.flt[5]) != 1000) return "global data table 3 does not start with type 1000";
  size_t at = c.flt[2];
  for (uint32_t i = 0; i < c.flt[6]; ++i) at += 8 + U32(c.flat, at + 4);
  if (at != c.flt[3]) return "global data table 1 does not end at table 2";
  for (uint32_t i = 0; i < c.flt[7]; ++i) at += 8 + U32(c.flat, at + 4);
  if (at != c.flt[4]) return "global data table 2 does not end at the change forms";
  for (uint32_t i = 0; i < c.flt[9]; ++i) {
    const uint8_t type = c.flat.at(at + 7);
    const size_t width = size_t(1) << (type >> 6);
    size_t len = 0;
    std::memcpy(&len, c.flat.data() + at + 9, width);
    at += 9 + 2 * width + len;
  }
  if (at != c.flt[5]) return "change forms do not end at global data table 3";
  // The table 3 count leaves out the Papyrus block (type 1001), so one more
  // block is read than counted (both readers do this).
  for (uint32_t i = 0; i < c.flt[8] + 1; ++i) at += 8 + U32(c.flat, at + 4);
  if (at != c.flt[0]) return "global data table 3 does not end at the form id array";
  const uint32_t ids = U32(c.flat, at);
  at += 4 + 4 * size_t(ids);
  const uint32_t visited = U32(c.flat, at);
  at += 4 + 4 * size_t(visited);
  if (at != c.flt[1]) return "the unknown table does not follow the visited worldspaces";
  if (at + 4 + U32(c.flat, at) != c.flat.size()) return "the unknown table does not end the file";
  return "";
}

// Builds a Special Edition container around a Legendary Edition save, for
// tests only. The body is the Legendary Edition body relabelled: its Papyrus
// block keeps 16-bit string indices, which a Special Edition reader takes as
// 32-bit (ReSaver StringTable, ESS.isStr32), so the game would not load this.
// It exercises savefile, which copies that block without reading it.
inline std::vector<uint8_t> SpecialEditionFixture(const std::vector<uint8_t>& le,
                                                   const std::vector<std::string>& lightPlugins,
                                                   uint16_t compression)
{
  const Container c = Parse(le);
  if (c.version >= 12) throw std::runtime_error("already a Special Edition save");
  std::vector<uint8_t> out(le.begin(), le.begin() + 13);
  PutU32(out, c.headerSize + 2);
  const size_t headerStart = out.size();
  out.insert(out.end(), le.begin() + 17, le.begin() + 17 + c.headerSize);
  const uint32_t version = 12;
  std::memcpy(out.data() + headerStart, &version, 4);
  PutU16(out, compression);
  const uint8_t* shot = le.data() + 17 + c.headerSize;
  for (size_t p = 0; p < size_t(c.shotWidth) * c.shotHeight; ++p) {
    out.insert(out.end(), shot + 3 * p, shot + 3 * p + 3);
    out.push_back(0xFF);
  }
  std::vector<uint8_t> body;
  body.push_back(78);
  std::vector<uint8_t> lists;
  lists.push_back(static_cast<uint8_t>(c.plugins.size()));
  for (auto& p : c.plugins) PutWString(lists, p);
  PutU16(lists, static_cast<uint16_t>(lightPlugins.size()));
  for (auto& p : lightPlugins) PutWString(lists, p);
  PutU32(body, static_cast<uint32_t>(lists.size()));
  body.insert(body.end(), lists.begin(), lists.end());
  const int64_t shift = int64_t(out.size()) - int64_t(c.headerEnd) +
    int64_t(lists.size()) - int64_t(c.pluginInfoSize);
  for (int i = 0; i < 10; ++i) PutU32(body, i < 6 ? uint32_t(c.flt[i] + shift) : c.flt[i]);
  body.insert(body.end(), c.flat.begin() + c.fltAt + 40, c.flat.end());
  if (compression == 0) {
    out.insert(out.end(), body.begin(), body.end());
  } else {
    std::vector<uint8_t> packed;
    if (compression == 1) {
      uLongf len = compressBound(static_cast<uLong>(body.size()));
      packed.resize(len);
      if (compress2(packed.data(), &len, body.data(), static_cast<uLong>(body.size()), 1) != Z_OK)
        throw std::runtime_error("zlib failed");
      packed.resize(len);
    } else {
      packed = Lz4Literals(body);
    }
    PutU32(out, static_cast<uint32_t>(body.size()));
    PutU32(out, static_cast<uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
  }
  return out;
}

}
