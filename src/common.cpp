// Helpers that work on every platform. The system-specific ones are in common_win.cpp (Windows)
// and android/app/src/main/cpp/common_posix.cpp (Android).
#include "common.h"

#include <cstdio>
#include <cstdlib>

std::wstring g_baseDir, g_dataDir, g_webDir;

std::string jsonStr(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 2);
  o += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += (char)c;
        }
    }
  }
  o += '"';
  return o;
}

std::string fmtNum(double v, int prec) {
  char b[64];
  snprintf(b, sizeof b, "%.*f", prec, v);
  return b;
}

std::string hex64(uint64_t v) {
  char b[24];
  snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
  return b;
}

uint64_t parseHex64(const std::string& s) { return strtoull(s.c_str(), nullptr, 16); }

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    size_t b = s.find(sep, a);
    out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}

bool web3Name(const std::string& name) {
  return name == "index.html" || name == "app.js" || name == "app.css";
}

std::string toLower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}
