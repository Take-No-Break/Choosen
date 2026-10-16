// The page's API (state, cards, rating, settings ...), shared by every platform: the Windows HTTP
// server (main.cpp) and the Android app (android/app/src/main/cpp/jni_bridge.cpp) both call it.
#pragma once
#include <cstdlib>
#include <string>
#include <unordered_map>

struct ApiCall {
  std::string method, path, body;
  std::unordered_map<std::string, std::string> params;  // decoded query parameters
  std::string param(const char* k, const std::string& def = "") const {
    auto it = params.find(k);
    return it == params.end() ? def : it->second;
  }
  int paramInt(const char* k, int def) const {
    auto it = params.find(k);
    return it == params.end() || it->second.empty() ? def : atoi(it->second.c_str());
  }
};

// Answers one of the shared routes into `body` (and `status`); false if `q.path` is not one of them.
bool apiRoute(const ApiCall& q, int& status, std::string& body);

// Decodes "a=1&b=x%20y" into params.
void parseQueryInto(const std::string& query, std::unordered_map<std::string, std::string>& out);
