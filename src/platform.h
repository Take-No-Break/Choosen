// Operating-system services used by the core (core*.cpp), which itself contains no system calls.
// platform_win.cpp implements them for Windows; a port (e.g. Android) provides the same functions.
// Paths are wide strings as in common.h, joined with '\' by the core.
#pragma once
#include "common.h"

struct DirEntry {
  std::wstring name;
  bool dir = false;
  bool hidden = false;  // hidden, system file or link: never scanned
  int64_t size = 0;
  uint64_t mtime = 0;   // last write time in the platform's own unit (only compared for equality)
};
// Entries of a folder without "." and ".."; false if it cannot be read.
bool platListDir(const std::wstring& dir, std::vector<DirEntry>& out);

struct PlatFile;  // an open file
enum class PlatOpen { Read, ReadWrite, Append };  // Append creates the file if it is missing
PlatFile* platOpen(const std::wstring& path, PlatOpen mode);
void platClose(PlatFile* f);
int64_t platSize(PlatFile* f);
size_t platReadAt(PlatFile* f, int64_t off, void* buf, size_t n);  // bytes read, 0 on error
bool platWriteAt(PlatFile* f, int64_t off, const std::string& data);
bool platTruncate(PlatFile* f, int64_t size);
bool platFlush(PlatFile* f);  // forces written bytes to the disk

int64_t platFileSize(const std::wstring& path);  // -1 if missing
bool platMakeDir(const std::wstring& path);      // true if it exists afterwards
bool platCopyFile(const std::wstring& from, const std::wstring& to);
// Export (core_export.cpp): copy or move one file; both fail if `to` already exists, so nothing is
// ever overwritten. A move to another drive copies and then deletes the original.
bool platCopyNoReplace(const std::wstring& from, const std::wstring& to);
bool platMoveNoReplace(const std::wstring& from, const std::wstring& to);
bool platDeleteFile(const std::wstring& path);
bool platRemoveEmptyDir(const std::wstring& path);  // a folder with anything in it stays
std::string platFullPath(const std::string& path);  // absolute, ".." resolved (UTF-8)
void platLocalDate(int64_t unixMs, int& year, int& month, int& day);
uint32_t platProcessId();

// One app at a time per data folder: Version 1 and Version 2 must never write one rating journal
// together. platLock holds dir\choosen.lock until platUnlock or the end of the process (also after
// a crash). nullptr if another app holds it; holder then names that app ("" if unknown). If the
// folder cannot hold a lock file at all, the result is an empty lock and the app goes on.
struct PlatLock;
PlatLock* platLock(const std::wstring& dir, const std::string& me, std::string& holder);
void platUnlock(PlatLock* l);
void platSleepMs(int ms);
