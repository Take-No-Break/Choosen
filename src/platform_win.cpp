// Windows implementation of platform.h.
#include "win.h"

#include "platform.h"

bool platListDir(const std::wstring& dir, std::vector<DirEntry>& out) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return false;
  do {
    std::wstring nm = fd.cFileName;
    if (nm == L"." || nm == L"..") continue;
    DirEntry e;
    e.name = nm;
    e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.hidden = (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) != 0;
    e.size = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    e.mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
    out.push_back(std::move(e));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return true;
}

struct PlatLock {
  HANDLE h = INVALID_HANDLE_VALUE;
};

PlatLock* platLock(const std::wstring& dir, const std::string& me, std::string& holder) {
  holder.clear();
  const std::wstring path = dir + L"\\choosen.lock";
  // Others may read who holds it, but nobody else may open it for writing while this handle lives.
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (GetLastError() != ERROR_SHARING_VIOLATION) return new PlatLock();  // no lock file possible
    HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r != INVALID_HANDLE_VALUE) {
      char buf[256];
      DWORD n = 0;
      if (ReadFile(r, buf, sizeof buf, &n, nullptr)) holder.assign(buf, n);
      CloseHandle(r);
    }
    return nullptr;
  }
  DWORD n = 0;
  SetEndOfFile(h);  // the file pointer is at 0: drop the previous holder's name
  WriteFile(h, me.data(), (DWORD)me.size(), &n, nullptr);
  auto* l = new PlatLock();
  l->h = h;
  return l;
}

void platUnlock(PlatLock* l) {
  if (!l) return;
  if (l->h != INVALID_HANDLE_VALUE) CloseHandle(l->h);
  delete l;
}

struct PlatFile {
  HANDLE h;
};

PlatFile* platOpen(const std::wstring& path, PlatOpen mode) {
  DWORD access = mode == PlatOpen::Read ? GENERIC_READ : mode == PlatOpen::Append ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE;
  DWORD share = mode == PlatOpen::Read ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ;
  HANDLE h = CreateFileW(longPath(path).c_str(), access, share, nullptr, mode == PlatOpen::Append ? OPEN_ALWAYS : OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  return h == INVALID_HANDLE_VALUE ? nullptr : new PlatFile{h};
}

void platClose(PlatFile* f) {
  if (!f) return;
  CloseHandle(f->h);
  delete f;
}

int64_t platSize(PlatFile* f) {
  LARGE_INTEGER sz{};
  return GetFileSizeEx(f->h, &sz) ? sz.QuadPart : -1;
}

size_t platReadAt(PlatFile* f, int64_t off, void* buf, size_t n) {
  LARGE_INTEGER li;
  li.QuadPart = off;
  if (!SetFilePointerEx(f->h, li, nullptr, FILE_BEGIN)) return 0;
  DWORD got = 0;
  if (!ReadFile(f->h, buf, (DWORD)n, &got, nullptr)) return 0;
  return got;
}

bool platWriteAt(PlatFile* f, int64_t off, const std::string& data) {
  LARGE_INTEGER li;
  li.QuadPart = off;
  if (!SetFilePointerEx(f->h, li, nullptr, FILE_BEGIN)) return false;
  DWORD put = 0;
  return WriteFile(f->h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
}

bool platTruncate(PlatFile* f, int64_t size) {
  LARGE_INTEGER li;
  li.QuadPart = size;
  return SetFilePointerEx(f->h, li, nullptr, FILE_BEGIN) && SetEndOfFile(f->h);
}

bool platFlush(PlatFile* f) { return FlushFileBuffers(f->h) != 0; }

int64_t platFileSize(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return -1;
  return ((int64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

bool platMakeDir(const std::wstring& path) {
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool platCopyFile(const std::wstring& from, const std::wstring& to) { return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0; }

bool platCopyNoReplace(const std::wstring& from, const std::wstring& to) {
  return CopyFileW(longPath(from).c_str(), longPath(to).c_str(), TRUE) != 0;
}

bool platMoveNoReplace(const std::wstring& from, const std::wstring& to) {
  return MoveFileExW(longPath(from).c_str(), longPath(to).c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
}

bool platDeleteFile(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }
bool platRemoveEmptyDir(const std::wstring& path) { return RemoveDirectoryW(longPath(path).c_str()) != 0; }

std::string platFullPath(const std::string& path) {
  std::wstring w = widen(path);
  wchar_t buf[4096];
  DWORD n = GetFullPathNameW(w.c_str(), 4096, buf, nullptr);
  return n > 0 && n < 4096 ? narrow(std::wstring(buf, n)) : path;
}

void platLocalDate(int64_t unixMs, int& year, int& month, int& day) {
  ULARGE_INTEGER u;
  u.QuadPart = (uint64_t)(unixMs + 11644473600000LL) * 10000ULL;
  FILETIME ft{u.LowPart, u.HighPart}, lft;
  SYSTEMTIME st;
  FileTimeToLocalFileTime(&ft, &lft);
  FileTimeToSystemTime(&lft, &st);
  year = st.wYear;
  month = st.wMonth;
  day = st.wDay;
}

uint32_t platProcessId() { return GetCurrentProcessId(); }

void platSleepMs(int ms) { Sleep(ms); }
