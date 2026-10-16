// Minimal HTTP/1.1 server on Winsock: keep-alive, one thread per connection,
// file streaming and a long-lived stream mode (used for Server-Sent Events).
#pragma once
#include "win.h"
#include "api_routes.h"
#include "common.h"

#include <atomic>
#include <functional>
#include <unordered_map>

// Method, path, body and decoded query parameters (api_routes.h) plus what HTTP adds.
struct HttpRequest : ApiCall {
  std::string query;
  std::unordered_map<std::string, std::string> headers;  // lower-cased names
};

struct HttpResponse {
  int status = 200;
  std::string type = "application/json; charset=utf-8";
  std::string body;
  std::wstring file;                    // when set, the file is streamed instead of body
  std::string cache = "no-store";       // Cache-Control value
  std::function<void(SOCKET)> stream;   // when set, takes over the socket (SSE)
  std::string headers;                  // extra header lines, each ending in "\r\n"
};

using HttpHandler = std::function<void(HttpRequest&, HttpResponse&)>;

// Binds 127.0.0.1:port (or every network interface), INVALID_SOCKET on failure.
SOCKET httpListen(int port, bool allInterfaces = false);
// Serves connections until the listener is closed while *stop is true.
void httpRun(SOCKET listener, HttpHandler handler, std::atomic<bool>* stop = nullptr);
bool httpSendAll(SOCKET s, const char* data, size_t n);
std::string urlDecode(const std::string& s);
