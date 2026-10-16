#include "win.h"

#include "remote.h"

#include "common.h"
#include "core.h"
#include "qr.h"

#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>

namespace {

std::mutex rmu;
std::unordered_set<std::string> tokens;  // paired phones
std::string pairCode;
int64_t pairUntil = 0;
int pairTries = 0;
int pairedCount = 0;  // pairings since the start: the PC's QR window closes when it grows
const int kPairMinutes = 5, kPairTries = 5;

HttpHandler handler;
SOCKET listener = INVALID_SOCKET;
std::atomic<bool> stopping{false};
int listenPort = 0;
std::string listenError;

std::wstring devicesPath() { return g_dataDir + L"\\remote-devices.txt"; }

std::string randomHex(size_t bytes) {
  std::random_device rd;  // MSVC: the system's cryptographic random source
  static const char* hex = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < bytes; ++i) {
    unsigned v = rd() & 0xff;
    s += hex[v >> 4];
    s += hex[v & 15];
  }
  return s;
}

void saveDevices() {
  std::string s = "# Choosen: phones allowed to connect (one token per line). Delete this file to unpair all.\n";
  for (auto& t : tokens) s += t + "\n";
  writeFileAtomic(devicesPath(), s);
}

}  // namespace

std::string remoteToken(const HttpRequest& q) {
  auto c = q.headers.find("cookie");
  if (c == q.headers.end()) return "";
  const std::string& v = c->second;
  size_t p = v.find("choosen_token=");
  if (p == std::string::npos || (p > 0 && v[p - 1] != ' ' && v[p - 1] != ';')) return "";
  p += 14;
  size_t e = v.find(';', p);
  return v.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

namespace {

bool isIpv4Host(const std::string& host, int port) {
  const std::string suffix = ":" + std::to_string(port);
  if (host.size() <= suffix.size() || host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
  const std::string ip = host.substr(0, host.size() - suffix.size());
  in_addr a;
  return inet_pton(AF_INET, ip.c_str(), &a) == 1;
}

}  // namespace

void remoteInit(HttpHandler h) {
  handler = std::move(h);
  std::string s;
  if (readFileBytes(devicesPath(), s)) {
    std::lock_guard<std::mutex> lk(rmu);
    for (auto line : split(s, '\n')) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() == 64 && line[0] != '#') tokens.insert(line);
    }
  }
  remoteApply();
}

void remoteApply() {
  std::lock_guard<std::mutex> lk(rmu);
  const bool want = g_cfg.remote;
  if (want && listener == INVALID_SOCKET) {
    listenPort = g_cfg.port + 1;
    listener = httpListen(listenPort, true);
    if (listener == INVALID_SOCKET) {
      listenError = "ポート " + std::to_string(listenPort) + " を開けません（他のプログラムが使っている可能性があります）";
      logLine("remote: cannot listen on port " + std::to_string(listenPort));
      return;
    }
    listenError.clear();
    stopping = false;
    SOCKET ls = listener;
    std::thread([ls] { httpRun(ls, handler, &stopping); }).detach();
    logLine("remote: listening on port " + std::to_string(listenPort));
  } else if (!want && listener != INVALID_SOCKET) {
    stopping = true;
    closesocket(listener);
    listener = INVALID_SOCKET;
    pairCode.clear();
    logLine("remote: stopped");
  }
}

bool remoteListening() {
  std::lock_guard<std::mutex> lk(rmu);
  return listener != INVALID_SOCKET;
}

int remotePort() {
  std::lock_guard<std::mutex> lk(rmu);
  return listenPort ? listenPort : g_cfg.port + 1;
}

std::vector<std::string> remoteAddresses() {
  std::vector<std::string> out;
  char name[256] = {};
  if (gethostname(name, sizeof name) != 0) return out;
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  if (getaddrinfo(name, nullptr, &hints, &res) != 0) return out;
  const int port = remotePort();
  for (addrinfo* p = res; p; p = p->ai_next) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, ip, sizeof ip);
    std::string s = ip;
    if (s.rfind("127.", 0) == 0 || s.rfind("169.254.", 0) == 0) continue;
    std::string url = "http://" + s + ":" + std::to_string(port) + "/";
    if (std::find(out.begin(), out.end(), url) == out.end()) out.push_back(url);
  }
  freeaddrinfo(res);
  // The home network first: 192.168.x, then 10.x, then the rest (often virtual adapters).
  auto rank = [](const std::string& u) { return u.rfind("http://192.168.", 0) == 0 ? 0 : u.rfind("http://10.", 0) == 0 ? 1 : 2; };
  std::stable_sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
  return out;
}

std::string remoteStartPairing() {
  std::lock_guard<std::mutex> lk(rmu);
  std::random_device rd;
  char b[8];
  snprintf(b, sizeof b, "%06u", (unsigned)(rd() % 1000000));
  pairCode = b;
  pairUntil = nowMs() + kPairMinutes * 60000LL;
  pairTries = 0;
  return pairCode;
}

bool remotePair(const std::string& code, std::string& token, const std::string& previous) {
  std::lock_guard<std::mutex> lk(rmu);
  if (pairCode.empty() || nowMs() > pairUntil) {
    pairCode.clear();
    return false;
  }
  if (code != pairCode) {
    if (++pairTries >= kPairTries) pairCode.clear();  // guessing is not an option
    return false;
  }
  pairCode.clear();  // one code, one phone
  if (!previous.empty()) tokens.erase(previous);  // the same phone scanned again: one registration
  token = randomHex(32);
  tokens.insert(token);
  ++pairedCount;
  saveDevices();
  logLine("remote: a phone was paired (" + std::to_string(tokens.size()) + " in total)");
  return true;
}

bool remoteAuthorized(const HttpRequest& q) {
  const std::string t = remoteToken(q);
  if (t.size() != 64) return false;
  std::lock_guard<std::mutex> lk(rmu);
  return tokens.count(t) > 0;
}

int remoteDeviceCount() {
  std::lock_guard<std::mutex> lk(rmu);
  return (int)tokens.size();
}

void remoteForgetAll() {
  std::lock_guard<std::mutex> lk(rmu);
  tokens.clear();
  pairCode.clear();
  saveDevices();
  logLine("remote: all phones unpaired");
}

std::string remoteStateJson(bool v3) {
  auto addrs = remoteAddresses();
  std::lock_guard<std::mutex> lk(rmu);
  std::string o = std::string("{\"on\":") + (g_cfg.remote ? "true" : "false") +
                  ",\"listening\":" + (listener != INVALID_SOCKET ? "true" : "false") +
                  ",\"error\":" + jsonStr(listenError) + ",\"devices\":" + std::to_string(tokens.size()) +
                  ",\"paired\":" + std::to_string(pairedCount) + ",\"urls\":[";
  for (size_t i = 0; i < addrs.size(); ++i) o += (i ? "," : "") + jsonStr(addrs[i]);
  const int64_t left = pairCode.empty() ? 0 : std::max<int64_t>(0, pairUntil - nowMs());
  o += "],\"code\":" + jsonStr(left > 0 ? pairCode : std::string()) + ",\"codeSeconds\":" + std::to_string(left / 1000);
  // While a code is valid: the link that pairs a phone in one step, and its QR code.
  if (left > 0 && !addrs.empty()) {
    const std::string link = addrs[0] + (v3 ? "v3/" : "") + "#pair=" + pairCode;
    o += ",\"link\":" + jsonStr(link) + ",\"qr\":[";
    const auto rows = qrEncode(link);
    for (size_t i = 0; i < rows.size(); ++i) o += (i ? ",\"" : "\"") + rows[i] + "\"";
    o += "]";
  }
  o += "}";
  return o;
}

std::string remoteCheckRequest(const HttpRequest& q, int port) {
  // The address must be typed as an IP: a host name could be an attacker's DNS pointing here.
  auto h = q.headers.find("host");
  if (h == q.headers.end() || !isIpv4Host(h->second, port)) return "host";
  auto origin = q.headers.find("origin");
  if (origin != q.headers.end() && origin->second != "http://" + h->second) return "origin";
  auto site = q.headers.find("sec-fetch-site");
  if (site != q.headers.end() && site->second != "same-origin" && site->second != "none") return "site";
  auto x = q.headers.find("x-choosen");
  if (q.method == "POST" && (x == q.headers.end() || x->second != "1")) return "post";
  return "";
}

bool remotePageFile(const std::string& p) {
  if (p == "/" || p == "/v3" || p == "/v3/") return true;
  if (p.rfind("/v3/", 0) == 0) return web3Name(p.substr(4));
  return p.size() > 1 && p.find('/', 1) == std::string::npos;  // the normal page's files
}

bool remoteAllowedPath(const std::string& method, const std::string& p) {
  static const char* blocked[] = {"/api/config", "/api/pickfolder", "/api/reveal", "/api/quit",
                                  "/api/rescan", "/api/hiddenCount", "/api/remote", "/api/export", "/api/reset", "/api/sort"};
  for (auto b : blocked)
    if (p == b || p.rfind(std::string(b) + "/", 0) == 0) return false;
  (void)method;
  return true;
}
