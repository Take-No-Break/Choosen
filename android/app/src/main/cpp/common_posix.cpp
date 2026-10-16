// Android (POSIX) implementations of the system-specific helpers declared in common.h.
// The core joins paths with '\' (its Windows heritage); posixPath turns them into '/' paths.
#include "common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

std::string posixPath(const std::wstring& path) {
  std::string s = narrow(path);
  for (auto& c : s)
    if (c == '\\') c = '/';
  return s;
}

std::wstring widen(const std::string& s) {  // UTF-8 -> UTF-32 (wchar_t is 32-bit on Android)
  std::wstring w;
  w.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) cp = c, n = 1;
    else if ((c >> 5) == 6) cp = c & 0x1f, n = 2;
    else if ((c >> 4) == 14) cp = c & 0x0f, n = 3;
    else if ((c >> 3) == 30) cp = c & 0x07, n = 4;
    else { ++i; continue; }
    if (i + n > s.size()) break;
    for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
    w += (wchar_t)cp;
    i += n;
  }
  return w;
}

std::string narrow(const std::wstring& w) {  // UTF-32 -> UTF-8
  std::string s;
  s.reserve(w.size());
  for (wchar_t wc : w) {
    uint32_t cp = (uint32_t)wc;
    if (cp < 0x80) {
      s += (char)cp;
    } else if (cp < 0x800) {
      s += (char)(0xc0 | (cp >> 6));
      s += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      s += (char)(0xe0 | (cp >> 12));
      s += (char)(0x80 | ((cp >> 6) & 0x3f));
      s += (char)(0x80 | (cp & 0x3f));
    } else {
      s += (char)(0xf0 | (cp >> 18));
      s += (char)(0x80 | ((cp >> 12) & 0x3f));
      s += (char)(0x80 | ((cp >> 6) & 0x3f));
      s += (char)(0x80 | (cp & 0x3f));
    }
  }
  return s;
}

int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void logLine(const std::string& s) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lk(mu);
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  char ts[64];
  strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S ", &lt);
  appendFileBytes(g_dataDir + L"\\log.txt", std::string(ts) + s + "\n");
}

bool readFileBytes(const std::wstring& path, std::string& out) {
  FILE* f = fopen(posixPath(path).c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  bool ok = !ferror(f);
  fclose(f);
  return ok;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data) {
  const std::string p = posixPath(path), tmp = p + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  size_t done = 0;
  while (done < data.size()) {
    ssize_t k = write(fd, data.data() + done, data.size() - done);
    if (k <= 0) {
      close(fd);
      return false;
    }
    done += (size_t)k;
  }
  fsync(fd);
  close(fd);
  return rename(tmp.c_str(), p.c_str()) == 0;
}

bool appendFileBytes(const std::wstring& path, const std::string& data) {
  int fd = open(posixPath(path).c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) return false;
  bool ok = write(fd, data.data(), data.size()) == (ssize_t)data.size();
  close(fd);
  return ok;
}

bool isDirectory(const std::wstring& path) {
  struct stat st;
  return stat(posixPath(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::wstring longPath(const std::wstring& path) { return path; }

// The page files are served by the app from its assets (MainActivity), not from here.
bool webResource(const std::string&, std::string&) { return false; }
