// Choosen — local photo ranking by swiping.
// Starts a tiny HTTP server on 127.0.0.1 and opens the UI in an Edge app window.
#include "win.h"  // must come first: sets up winsock2 before windows.h
#include "common.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <semaphore>
#include <thread>

#include "api_routes.h"
#include "core.h"
#include "http.h"
#include "platform.h"
#include "remote.h"
#include "selftest.h"
#include "thumbs.h"

namespace {

std::atomic<int> g_streams{0};
std::atomic<bool> g_everStreamed{false};
std::atomic<int64_t> g_lastStreamEnd{0};
std::atomic<int64_t> g_lastActivity{0};
int64_t g_startMs = 0;
bool g_stay = false;
bool g_diskWeb = false;  // --disk-web: serve web\ from disk instead of the copy inside the exe (development)
bool g_uiV3 = false;     // --ui v3: the window shows Choosen V3 (/v3/) instead of the Version 1 page

const char* mimeFor(const std::string& ext) {
  if (ext == "jpg" || ext == "jpeg" || ext == "jfif") return "image/jpeg";
  if (ext == "png") return "image/png";
  if (ext == "gif") return "image/gif";
  if (ext == "webp") return "image/webp";
  if (ext == "avif") return "image/avif";
  if (ext == "bmp") return "image/bmp";
  if (ext == "html") return "text/html; charset=utf-8";
  if (ext == "js") return "text/javascript; charset=utf-8";
  if (ext == "css") return "text/css; charset=utf-8";
  if (ext == "svg") return "image/svg+xml";
  return nullptr;
}

std::wstring edgePath() {
  const wchar_t* vars[] = {L"ProgramFiles(x86)", L"ProgramFiles", L"LOCALAPPDATA"};
  for (auto v : vars) {
    wchar_t buf[MAX_PATH];
    if (!GetEnvironmentVariableW(v, buf, MAX_PATH)) continue;
    std::wstring p = std::wstring(buf) + L"\\Microsoft\\Edge\\Application\\msedge.exe";
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
  }
  return L"";
}

// Asks whatever listens on the port whether it is Choosen. Also counts as activity for a running
// Choosen, so it will not exit while the new window is still starting.
bool probeChoosen(int port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  DWORD tmo = 2000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof tmo);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons((u_short)port);
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  bool ok = false;
  if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
    std::string req = "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nConnection: close\r\n\r\n";
    if (httpSendAll(s, req.data(), req.size())) {
      std::string resp;
      char buf[4096];
      int n;
      while (resp.size() < 65536 && (n = recv(s, buf, sizeof buf, 0)) > 0) {
        resp.append(buf, n);
        if (resp.find("\"app\":\"choosen\"") != std::string::npos) {
          ok = true;
          break;
        }
      }
    }
  }
  closesocket(s);
  return ok;
}

void openWindow(int port) {
  std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + (g_uiV3 ? L"/v3/" : L"/");
  std::wstring edge = edgePath();
  if (!edge.empty()) {
    std::wstring cmd = L"\"" + edge + L"\" --app=" + url + L" --window-size=1400,920";
    if (g_uiV3) {
      // its own Edge profile: Version 1's page adopts and clears every rating queue of its profile
      wchar_t local[MAX_PATH] = {};
      GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
      cmd += L" --user-data-dir=\"" + std::wstring(local) + L"\\Choosen\\Edge-v3\" --no-first-run --no-default-browser-check";
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return;
    }
  }
  ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Server-Sent Events: pushes state whenever it changes. The open connection also tells us the
// window is alive; when every window is closed the server exits on its own.
void eventStream(SOCKET s) {
  g_streams++;
  g_everStreamed = true;
  uint64_t sentVer = 0;
  int64_t lastSend = 0;
  for (;;) {
    uint64_t v = coreVersion();
    int64_t now = nowMs();
    if (v != sentVer) {
      std::string msg = "data: " + apiState() + "\n\n";
      if (!httpSendAll(s, msg.data(), msg.size())) break;
      sentVer = v;
      lastSend = now;
    } else if (now - lastSend > 15000) {
      if (!httpSendAll(s, ": ping\n\n", 8)) break;
      lastSend = now;
    }
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{0, 150000};
    if (select(0, &rd, nullptr, nullptr, &tv) > 0) {
      char tmp[256];
      if (recv(s, tmp, sizeof tmp, 0) <= 0) break;  // browser closed the connection
    }
  }
  g_lastStreamEnd = nowMs();  // before the decrement, so the watchdog never sees 0 with a stale time
  g_streams--;
}

// Exits once no window has been connected for 20 s and nothing else asked for anything. Before the
// first window ever connects it waits up to 3 minutes (Edge can be slow after an update).
void watchdog() {
  for (;;) {
    Sleep(2000);
    coreDailyBackup();  // the app may stay open for days
    if (g_stay || g_cfg.remote) continue;  // phones may connect any time: keep serving
    int64_t now = nowMs();
    bool idle = g_streams == 0 && now - g_lastStreamEnd > 20000 && now - g_lastActivity > 20000;
    bool pastGrace = g_everStreamed ? true : now - g_startMs > 180000;
    if (idle && pastGrace) {
      logLine("no window open, exiting");
      coreSaveIndex();
      ExitProcess(0);
    }
  }
}

void serveStatic(const std::string& name, HttpResponse& r) {
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    if (!ok) {
      r.status = 404;
      return;
    }
  }
  std::string ext = name.substr(name.rfind('.') + 1);
  const char* mime = mimeFor(ext);
  if (!mime) {
    r.status = 404;
    return;
  }
  r.type = mime;
  r.cache = "no-cache";
  if (g_diskWeb) {
    r.file = g_webDir + L"\\" + widen(name);
  } else if (!webResource(name, r.body)) {
    r.status = 404;
    r.type = "application/json; charset=utf-8";
    r.body = "{}";
  }
}

// Choosen V3's three files. Nothing is loaded from elsewhere: the policy below lets the page use
// only this server (no other site, no inline script).
void serveV3(const std::string& name, HttpResponse& r) {
  if (!web3Name(name)) {
    r.status = 404;
    r.body = "{}";
    return;
  }
  r.type = mimeFor(name.substr(name.rfind('.') + 1));
  r.cache = "no-cache";
  r.headers +=
      "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
      "img-src 'self' blob: data:; connect-src 'self'; worker-src 'self' blob:; object-src 'none'; "
      "base-uri 'none'; frame-ancestors 'none'\r\n";
  if (g_diskWeb) {
    r.file = g_baseDir + L"\\web3\\dist\\" + widen(name);
  } else if (!webResource("v3/" + name, r.body)) {
    r.status = 404;
    r.type = "application/json; charset=utf-8";
    r.body = "{}";
  }
}

void route(HttpRequest& q, HttpResponse& r, bool remote);

void handle(HttpRequest& q, HttpResponse& r) {
  // Only answer our own page: the Host check blocks DNS rebinding; the Origin / Sec-Fetch-Site
  // checks stop other websites from embedding the photos or keeping the server alive.
  const std::string port = std::to_string(g_cfg.port);
  const std::string& host = q.headers["host"];
  if (host != "127.0.0.1:" + port && host != "localhost:" + port) {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  auto origin = q.headers.find("origin");
  if (origin != q.headers.end() && origin->second != "http://127.0.0.1:" + port &&
      origin->second != "http://localhost:" + port) {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  auto site = q.headers.find("sec-fetch-site");
  if (site != q.headers.end() && site->second != "same-origin" && site->second != "none") {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  g_lastActivity = nowMs();
  if (q.method == "POST" && q.headers["x-choosen"] != "1") {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  route(q, r, false);
}

// Requests to the phone listener (remote.h): checked, paired, and limited to swiping, browsing
// and rating.
void handleRemote(HttpRequest& q, HttpResponse& r) {
  if (!g_cfg.remote || !remoteCheckRequest(q, remotePort()).empty()) {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  g_lastActivity = nowMs();
  const std::string& p = q.path;
  if (p == "/api/pair" && q.method == "POST") {
    std::string token;
    if (!remotePair(q.param("code"), token, remoteToken(q))) {
      r.status = 403;
      r.body = "{\"ok\":false}";
      return;
    }
    r.headers = "Set-Cookie: choosen_token=" + token + "; Path=/; Max-Age=31536000; HttpOnly; SameSite=Strict\r\n";
    r.body = "{\"ok\":true}";
    return;
  }
  if (!remotePageFile(p) && !remoteAuthorized(q)) {
    r.status = 401;
    r.body = "{\"error\":\"pair\"}";
    return;
  }
  if (!remoteAllowedPath(q.method, p)) {
    r.status = 403;
    r.body = "{\"error\":\"forbidden\"}";
    return;
  }
  route(q, r, true);
}

void route(HttpRequest& q, HttpResponse& r, bool remote) {
  const std::string& p = q.path;

  if (p == "/") return serveStatic("index.html", r);
  if (p.rfind("/img/", 0) == 0 || p.rfind("/thumb/", 0) == 0) {
    bool thumb = p[1] == 't';
    int id = atoi(p.c_str() + (thumb ? 7 : 5));
    std::wstring path;
    uint64_t fp;
    std::string ext;
    if (!itemFile(id, q.param("v"), path, fp, ext)) {
      r.status = 404;
      r.body = "{}";
      return;
    }
    const char* mime = mimeFor(ext);
    // ?w=: a screen-fit JPEG for phones (a 24 MP picture decodes to ~96 MB, too much for a phone);
    // animated GIFs stay originals
    const int fit = thumb || ext == "gif" ? 0 : q.paramInt("w", 0);
    if (!thumb && mime && !fit) {  // the browser decodes these itself: stream the original file
      r.file = path;
      r.type = mime;
      r.cache = "no-store";  // no second copy in the browser's disk cache
      return;
    }
    int size = thumb ? std::clamp(q.paramInt("s", 320), 64, 1024) : fit > 0 ? std::clamp(fit, 480, 2560) : 2560;
    std::string jpg;
    // at most two large decodes at once: a phone's prefetch must not tie up every core
    static std::counting_semaphore<2> bigDecodes(2);
    const bool big = size > 1024;
    if (big) bigDecodes.acquire();
    const bool made = shellJpeg(path, fp, size, jpg);
    if (big) bigDecodes.release();
    if (!made && fit && mime) {  // no shell decoder for it: the original after all
      r.file = path;
      r.type = mime;
      r.cache = "no-store";
      return;
    }
    if (!made) {
      r.status = 404;
      r.body = "{}";
      return;
    }
    r.type = "image/jpeg";
    r.body = std::move(jpg);
    r.cache = "no-store";  // kept in Choosen's memory cache only, never in the browser's disk cache
    return;
  }
  if (p == "/api/events") {
    r.stream = eventStream;
    return;
  }
  if (apiRoute(q, r.status, r.body)) {  // the routes every platform shares (api_routes.cpp)
    if (p == "/api/state" && remote) r.body = "{\"remote\":true," + r.body.substr(1);  // a phone hides settings
    if (p == "/api/config") remoteApply();
    return;
  }
  if (p == "/api/remote") {
    r.body = remoteStateJson(q.param("ui") == "v3");
    return;
  }
  if (p == "/api/remote/pair" && q.method == "POST") {
    r.body = "{\"code\":" + jsonStr(remoteStartPairing()) + "}";
    return;
  }
  if (p == "/api/remote/forget" && q.method == "POST") {
    remoteForgetAll();
    r.body = "{\"ok\":true}";
    return;
  }
  if (p == "/api/reveal" && q.method == "POST") {
    std::wstring path;
    uint64_t fp;
    std::string ext;
    if (itemFile(q.paramInt("id", -1), q.param("fp"), path, fp, ext)) revealInExplorer(path);
    r.body = "{\"ok\":true}";
    return;
  }
  if (p == "/api/pickfolder" && q.method == "POST") {
    std::wstring picked = pickFolder(widen(g_cfg.root));
    r.body = "{\"path\":" + jsonStr(narrow(picked)) + "}";
    return;
  }
  if (p == "/api/quit" && q.method == "POST") {
    coreSaveIndex();
    r.body = "{\"ok\":true}";
    std::thread([] {
      Sleep(300);
      ExitProcess(0);
    }).detach();
    return;
  }
  if (p == "/v3" || p == "/v3/") return serveV3("index.html", r);
  if (p.rfind("/v3/", 0) == 0 && p.find('/', 4) == std::string::npos) return serveV3(p.substr(4), r);
  if (p.size() > 1 && p.find('/', 1) == std::string::npos) return serveStatic(p.substr(1), r);
  r.status = 404;
  r.body = "{\"error\":\"not found\"}";
}

}  // namespace

int main() {
  g_startMs = nowMs();
  std::vector<wchar_t> exe(32768);  // long-path safe
  GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size());
  g_baseDir = exe.data();  // may carry a \\?\ prefix for very long paths; W APIs accept it
  g_baseDir = g_baseDir.substr(0, g_baseDir.find_last_of(L'\\'));
  g_dataDir = g_baseDir + L"\\data";
  g_webDir = g_baseDir + L"\\web";
  CreateDirectoryW(g_dataDir.c_str(), nullptr);

  bool noWindow = false;
  std::wstring tool;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--no-window") noWindow = true;
    if (a == L"--stay") g_stay = true;
    if (a == L"--disk-web") g_diskWeb = true;
    if (a == L"--ui" && i + 1 < argc && std::wstring(argv[i + 1]) == L"v3") g_uiV3 = true;
    if (a == L"--ui=v3") g_uiV3 = true;
    if (a == L"--selftest" || a == L"--bench" || a == L"--simulate") tool = a;
  }
  LocalFree(argv);
  if (!tool.empty()) return runTool(tool);  // developer tools; never touch data\ (see selftest.cpp)

  loadConfig();
  // One server per data folder: two servers appending to the same ratings.log would interleave
  // their tick counters. A second launch only opens a window onto the running one; that one may
  // still be starting, so keep asking for a while.
  uint64_t h = 1469598103934665603ULL;
  for (wchar_t c : g_dataDir) h = (h ^ (uint64_t)towlower(c)) * 1099511628211ULL;
  HANDLE single = CreateMutexW(nullptr, TRUE, (L"Local\\Choosen-" + widen(hex64(h))).c_str());
  if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
    for (int attempt = 0; attempt < 30; ++attempt) {
      for (int port = g_cfg.port; port < g_cfg.port + 10; ++port)
        if (probeChoosen(port)) {
          if (!noWindow) openWindow(port);
          return 0;
        }
      Sleep(1000);
    }
    MessageBoxW(nullptr, L"Choosen はすでに起動していますが応答がありません。\n少し待ってからもう一度起動してください。",
                L"Choosen", MB_ICONWARNING);
    return 1;
  }
  // Version 2 (or another app) may have this data folder open: then it stays theirs.
  std::string holder;
  if (!platLock(g_dataDir, "Choosen V1 (pid " + std::to_string(platProcessId()) + ")", holder)) {
    MessageBoxW(nullptr,
                holder.rfind("Choosen V2", 0) == 0
                    ? L"Choosen V2 が同じ評価データを使っています。\nChoosen V2 を閉じてから、もう一度開いてください。"
                    : L"別の Choosen が同じ評価データを使っています。\nそちらを閉じてから、もう一度開いてください。",
                L"Choosen", MB_ICONINFORMATION);
    return 1;
  }
  // Take the configured port; if it is busy, either Choosen already runs there (open a window onto
  // it) or some other program owns it (try the next port).
  SOCKET ls = INVALID_SOCKET;
  const int basePort = g_cfg.port;
  for (int port = basePort; port < basePort + 10; ++port) {
    ls = httpListen(port);
    if (ls != INVALID_SOCKET) {
      if (port != basePort) logLine("port " + std::to_string(basePort) + " is taken by another program; using " + std::to_string(port));
      g_cfg.port = port;
      break;
    }
    int err = WSAGetLastError();
    if (probeChoosen(port)) {
      if (!noWindow) openWindow(port);
      return 0;
    }
    logLine("cannot listen on port " + std::to_string(port) + " (error " + std::to_string(err) + ")");
  }
  if (ls == INVALID_SOCKET) {
    MessageBoxW(nullptr,
                L"Choosen を起動できません。\n通信用のポート (47800 番付近) を他のプログラムが使っています。\n"
                L"data\\config.txt の port= を別の番号 (例: 48800) に変えてから、もう一度起動してください。",
                L"Choosen", MB_ICONERROR);
    return 1;
  }
  // Answer requests right away (they wait for the index to load) so a second launch that asks
  // "is Choosen here?" during startup gets its answer.
  std::thread([ls] { httpRun(ls, handle); }).detach();
  remoteInit(handleRemote);  // the phone listener, if it is switched on
  if (!noWindow) openWindow(g_cfg.port);  // start the browser while we load the index
  coreInit();
  coreStartScan();
  std::thread(watchdog).detach();
  for (;;) Sleep(INFINITE);
}
