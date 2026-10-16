// Windows implementations of the system-specific helpers declared in common.h.
#include "win.h"

#include "common.h"

#include <cstdio>
#include <mutex>

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

int64_t nowMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (int64_t)(u.QuadPart / 10000) - 11644473600000LL;
}

void logLine(const std::string& s) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lk(mu);
  SYSTEMTIME st;
  GetLocalTime(&st);
  char ts[64];
  snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  appendFileBytes(g_dataDir + L"\\log.txt", std::string(ts) + s + "\r\n");
}

bool readFileBytes(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1LL << 31)) {
    CloseHandle(h);
    return false;
  }
  out.resize((size_t)sz.QuadPart);
  DWORD got = 0;
  BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
  CloseHandle(h);
  if (!ok) return false;
  out.resize(got);
  return true;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data) {
  std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr);
  FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) return false;
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool appendFileBytes(const std::wstring& path, const std::string& data) {
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr);
  CloseHandle(h);
  return ok != 0;
}

std::wstring longPath(const std::wstring& path) {
  if (path.size() < 240 || path.rfind(L"\\\\", 0) == 0) return path;
  return L"\\\\?\\" + path;
}

bool isDirectory(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool webResource(const std::string& name, std::string& out) {
  static const struct { const char* name; int id; } kFiles[] = {
      {"index.html", 101}, {"app.js", 102}, {"app.css", 103}, {"icon.png", 104},
      // Choosen V3, served at /v3/ (see web3Name)
      {"v3/index.html", 111}, {"v3/app.js", 112}, {"v3/app.css", 113}};
  for (const auto& f : kFiles) {
    if (name != f.name) continue;
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(f.id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const void* p = mem ? LockResource(mem) : nullptr;
    if (!p) return false;
    out.assign(static_cast<const char*>(p), SizeofResource(nullptr, res));
    return true;
  }
  return false;
}
