#include "Validators.h"

// Plugin file names routinely contain spaces and apostrophes: "Water for
// ENB.esm", "JK's Whiterun's Outskirts.esp". Rejecting those made getFileInfo
// throw on the first such name, which killed load order verification for any
// real mod list. Path separators, ':' and control characters stay forbidden,
// so this still cannot be walked out of the Data directory.
bool ValidateFilename(std::string_view filename, bool allowDots)
{
  for (char c : filename) {
    if (!(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') ||
          ('a' <= c && c <= 'z') || (c == '.' && allowDots) || c == '-' ||
          c == '_' || c == ' ' || c == '\'' || c == '(' || c == ')' ||
          c == '&' || c == '+' || c == ',' || c == '!')) {
      return false;
    }
  }
  return true;
}

bool ValidateRelativePath(std::string_view path)
{
  for (size_t i = 0; i < path.size(); ++i) {
    // Forbid everything including ':' and null character
    const char& c = path[i];
    if (!(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') ||
          ('a' <= c && c <= 'z') || c == '.' || c == '-' || c == '_' ||
          c == '/' || c == '\\')) {
      return false;
    }
    if (i > 0 && path[i - 1] == '.' && path[i] == '.') {
      return false;
    }
  }
  return true;
}
