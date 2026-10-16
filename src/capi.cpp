#if defined(_WIN32)
#include "win.h"  // must come first: sets up winsock2 before windows.h
#endif
#include "capi.h"

#include "api_routes.h"
#include "common.h"
#include "core.h"
#include "platform.h"

#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>

namespace {

char* dup(const std::string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (p) std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

std::string str(const char* s) { return s ? s : ""; }

std::mutex initMu;
PlatLock* dataLock = nullptr;  // held until the process ends

#if defined(_WIN32)
// Every Version 1 holds this mutex while it serves a data folder (see main.cpp), also the versions
// from before choosen.lock (0.7.2 and older), which would not see Version 2's lock.
bool versionOneRunning(const std::wstring& dataDir) {
  uint64_t h = 1469598103934665603ULL;
  for (wchar_t c : dataDir) h = (h ^ (uint64_t)towlower(c)) * 1099511628211ULL;
  HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, (L"Local\\Choosen-" + widen(hex64(h))).c_str());
  if (!m) return false;
  CloseHandle(m);
  return true;
}
#endif

}  // namespace

CHOOSEN_API char* choosen_init(const char* dataDir) {
  std::lock_guard<std::mutex> lk(initMu);
  if (!dataLock) {
    const std::wstring dir = widen(str(dataDir));
#if defined(_WIN32)
    if (versionOneRunning(dir)) return dup("Choosen V1");
#endif
    std::string holder;
    dataLock = platLock(dir, "Choosen V2 (pid " + std::to_string(platProcessId()) + ")", holder);
    if (!dataLock) return dup(holder.empty() ? "?" : holder);
    g_baseDir = g_dataDir = dir;
    loadConfig();
    coreInit();
    coreStartScan();
  }
  return dup("");
}

CHOOSEN_API char* choosen_api(const char* method, const char* path, const char* query, const char* body) {
  ApiCall q;
  q.method = str(method);
  q.path = str(path);
  q.body = str(body);
  parseQueryInto(str(query), q.params);
  int status = 200;
  std::string out;
  if (!apiRoute(q, status, out)) {
    status = 404;
    out = "{\"error\":\"not found\"}";
  }
  return dup(std::to_string(status) + "\n" + out);
}

CHOOSEN_API char* choosen_item_path(int id, const char* fp) {
  std::wstring path;
  uint64_t f;
  std::string ext;
  if (!itemFile(id, str(fp), path, f, ext)) return dup("");
#if defined(_WIN32)
  return dup(narrow(path));
#else
  std::string p = narrow(path);
  for (auto& c : p)
    if (c == '\\') c = '/';
  return dup(p);
#endif
}

CHOOSEN_API uint64_t choosen_version(void) { return coreVersion(); }

CHOOSEN_API void choosen_save_index(void) { coreSaveIndex(); }

CHOOSEN_API void choosen_daily_backup(void) { coreDailyBackup(); }

CHOOSEN_API void choosen_rescan(void) { coreStartScan(); }

CHOOSEN_API void choosen_free(char* s) { std::free(s); }
