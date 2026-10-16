#include "http.h"

#include <objbase.h>

#include <thread>

static const char* statusText(int s) {
  switch (s) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}

std::string urlDecode(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '+') {
      o += ' ';
    } else if (c == '%' && i + 2 < s.size()) {
      char h[3] = {s[i + 1], s[i + 2], 0};
      o += (char)strtol(h, nullptr, 16);
      i += 2;
    } else {
      o += c;
    }
  }
  return o;
}

bool httpSendAll(SOCKET s, const char* data, size_t n) {
  while (n > 0) {
    int k = send(s, data, (int)std::min<size_t>(n, 1 << 20), 0);
    if (k <= 0) return false;
    data += k;
    n -= (size_t)k;
  }
  return true;
}

SOCKET httpListen(int port, bool allInterfaces) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ls == INVALID_SOCKET) return INVALID_SOCKET;
  BOOL excl = TRUE;  // refuse to share the port with another process
  setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof excl);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons((u_short)port);
  if (allInterfaces)
    a.sin_addr.s_addr = htonl(INADDR_ANY);
  else
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, SOMAXCONN) != 0) {
    closesocket(ls);
    return INVALID_SOCKET;
  }
  return ls;
}

static void parseQuery(const std::string& q, std::unordered_map<std::string, std::string>& out) {
  if (q.empty()) return;
  for (auto& kv : split(q, '&')) {
    if (kv.empty()) continue;
    size_t eq = kv.find('=');
    if (eq == std::string::npos)
      out[urlDecode(kv)] = "";
    else
      out[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
  }
}

static bool sendFile(SOCKET s, HttpResponse& r) {
  HANDLE h = CreateFileW(longPath(r.file).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  LARGE_INTEGER sz{};
  if (h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h, &sz)) {
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    std::string hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    return httpSendAll(s, hdr.data(), hdr.size());
  }
  std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: " + r.type +
                    "\r\nContent-Length: " + std::to_string(sz.QuadPart) + "\r\nCache-Control: " + r.cache + "\r\n" +
                    r.headers + "Connection: keep-alive\r\n\r\n";
  bool ok = httpSendAll(s, hdr.data(), hdr.size());
  static thread_local std::vector<char> buf(512 * 1024);
  // Send exactly the announced length. If the file shrinks or a read fails midway, close the
  // connection so the browser fails fast instead of waiting for bytes that never come.
  int64_t remaining = sz.QuadPart;
  while (ok && remaining > 0) {
    DWORD want = (DWORD)std::min<int64_t>((int64_t)buf.size(), remaining);
    DWORD got = 0;
    if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) {
      ok = false;
      break;
    }
    ok = httpSendAll(s, buf.data(), got);
    remaining -= got;
  }
  CloseHandle(h);
  return ok;
}

// Handles one request on the connection. Returns false when the connection should close.
static bool serveOne(SOCKET s, std::string& buf, HttpHandler& handler) {
  char tmp[16384];
  size_t hdrEnd;
  while ((hdrEnd = buf.find("\r\n\r\n")) == std::string::npos) {
    if (buf.size() > 65536) return false;
    int n = recv(s, tmp, sizeof tmp, 0);
    if (n <= 0) return false;
    buf.append(tmp, n);
  }
  HttpRequest q;
  std::string head = buf.substr(0, hdrEnd);
  auto lines = split(head, '\n');
  {
    auto parts = split(lines[0], ' ');
    if (parts.size() < 2) return false;
    q.method = parts[0];
    std::string target = parts[1];
    size_t qm = target.find('?');
    q.path = urlDecode(target.substr(0, qm));
    if (qm != std::string::npos) q.query = target.substr(qm + 1);
  }
  for (size_t i = 1; i < lines.size(); ++i) {
    std::string l = lines[i];
    if (!l.empty() && l.back() == '\r') l.pop_back();
    size_t c = l.find(':');
    if (c == std::string::npos) continue;
    std::string v = l.substr(c + 1);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    q.headers[toLower(l.substr(0, c))] = v;
  }
  size_t clen = 0;
  if (auto it = q.headers.find("content-length"); it != q.headers.end()) clen = (size_t)atoll(it->second.c_str());
  if (clen > (64u << 20)) return false;
  size_t need = hdrEnd + 4 + clen;
  while (buf.size() < need) {
    int n = recv(s, tmp, sizeof tmp, 0);
    if (n <= 0) return false;
    buf.append(tmp, n);
  }
  q.body = buf.substr(hdrEnd + 4, clen);
  buf.erase(0, need);
  parseQuery(q.query, q.params);

  HttpResponse r;
  try {
    handler(q, r);
  } catch (...) {
    r = HttpResponse{};
    r.status = 500;
    r.body = "{\"error\":\"internal\"}";
  }

  if (r.stream) {
    std::string hdr =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: "
        "keep-alive\r\n\r\n";
    if (httpSendAll(s, hdr.data(), hdr.size())) r.stream(s);
    return false;
  }
  if (!r.file.empty()) return sendFile(s, r);

  std::string hdr = "HTTP/1.1 " + std::to_string(r.status) + " " + statusText(r.status) +
                    "\r\nContent-Type: " + r.type + "\r\nContent-Length: " + std::to_string(r.body.size()) +
                    "\r\nCache-Control: " + r.cache + "\r\n" + r.headers + "Connection: keep-alive\r\n\r\n";
  if (!httpSendAll(s, hdr.data(), hdr.size())) return false;
  return httpSendAll(s, r.body.data(), r.body.size());
}

void thumbsThreadCleanup();  // thumbs.cpp: releases this thread's WIC factory

static void connThread(SOCKET s, HttpHandler* handler) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // shell thumbnails + folder picker need STA
  DWORD tmo = 5 * 60 * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
  BOOL nodelay = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof nodelay);
  std::string buf;
  while (serveOne(s, buf, *handler)) {
  }
  closesocket(s);
  thumbsThreadCleanup();
  CoUninitialize();
}

void httpRun(SOCKET ls, HttpHandler handler, std::atomic<bool>* stop) {
  auto* h = new HttpHandler(std::move(handler));  // lives as long as its connections may
  for (;;) {
    SOCKET c = accept(ls, nullptr, nullptr);
    if (c == INVALID_SOCKET) {
      if (stop && *stop) return;
      continue;
    }
    std::thread(connThread, c, h).detach();
  }
}
