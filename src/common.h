// Shared helpers for Choosen (UTF-8 internally). No system headers here: files that call Windows
// include win.h themselves, so the core (core*.cpp) cannot use Windows by accident.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern std::wstring g_baseDir;  // folder containing Choosen.exe
extern std::wstring g_dataDir;  // <base>\data
extern std::wstring g_webDir;   // <base>\web

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);
int64_t nowMs();
void logLine(const std::string& s);

std::string jsonStr(const std::string& s);  // quoted + escaped JSON string
std::string fmtNum(double v, int prec = 1);
std::string hex64(uint64_t v);
uint64_t parseHex64(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
std::string toLower(std::string s);
// The files of Choosen V3 served at /v3/<name>: exactly the names of its build (web3\dist).
bool web3Name(const std::string& name);

bool readFileBytes(const std::wstring& path, std::string& out);
bool writeFileAtomic(const std::wstring& path, const std::string& data);
bool appendFileBytes(const std::wstring& path, const std::string& data);
bool isDirectory(const std::wstring& path);
// A page file built into the exe (res\choosen.rc): index.html, app.js, app.css, icon.png.
bool webResource(const std::string& name, std::string& out);
// Adds the \\?\ prefix to absolute paths near MAX_PATH so CreateFileW can open them.
std::wstring longPath(const std::wstring& path);
