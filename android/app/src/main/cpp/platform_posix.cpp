// POSIX implementation of platform.h: Android, and iOS through ios/Choosen/Core/platform_ios.mm.
#include "platform.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <ctime>

std::string posixPath(const std::wstring& path);  // common_posix.cpp

struct PlatLock {
  int fd = -1;
};

PlatLock* platLock(const std::wstring& dir, const std::string& me, std::string& holder) {
  holder.clear();
  const int fd = open((posixPath(dir) + "/choosen.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return new PlatLock();  // no lock file possible
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    const bool held = errno == EWOULDBLOCK;
    char buf[256];
    const ssize_t n = pread(fd, buf, sizeof buf, 0);
    if (n > 0) holder.assign(buf, (size_t)n);
    close(fd);
    return held ? nullptr : new PlatLock();
  }
  if (ftruncate(fd, 0) == 0 && pwrite(fd, me.data(), me.size(), 0) < 0) holder.clear();
  auto* l = new PlatLock();
  l->fd = fd;
  return l;
}

void platUnlock(PlatLock* l) {
  if (!l) return;
  if (l->fd >= 0) close(l->fd);
  delete l;
}

bool platListDir(const std::wstring& dir, std::vector<DirEntry>& out) {
  const std::string d = posixPath(dir);
  DIR* h = opendir(d.c_str());
  if (!h) return false;
  while (dirent* e = readdir(h)) {
    std::string nm = e->d_name;
    if (nm == "." || nm == "..") continue;
    struct stat st;
    if (lstat((d + "/" + nm).c_str(), &st) != 0) continue;
    DirEntry x;
    x.name = widen(nm);
    x.dir = S_ISDIR(st.st_mode);
    x.hidden = S_ISLNK(st.st_mode);  // links are never followed (no loops, no double counting)
    x.size = st.st_size;
#if defined(__APPLE__)  // iOS (ios/): Darwin names the field st_mtimespec
    x.mtime = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL + (uint64_t)st.st_mtimespec.tv_nsec;
#else
    x.mtime = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL + (uint64_t)st.st_mtim.tv_nsec;
#endif
    out.push_back(std::move(x));
  }
  closedir(h);
  return true;
}

struct PlatFile {
  int fd;
};

PlatFile* platOpen(const std::wstring& path, PlatOpen mode) {
  int flags = mode == PlatOpen::Read ? O_RDONLY : mode == PlatOpen::Append ? (O_RDWR | O_CREAT) : O_RDWR;
  int fd = open(posixPath(path).c_str(), flags, 0600);
  return fd < 0 ? nullptr : new PlatFile{fd};
}

void platClose(PlatFile* f) {
  if (!f) return;
  close(f->fd);
  delete f;
}

int64_t platSize(PlatFile* f) {
  struct stat st;
  return fstat(f->fd, &st) == 0 ? (int64_t)st.st_size : -1;
}

size_t platReadAt(PlatFile* f, int64_t off, void* buf, size_t n) {
  ssize_t k = pread(f->fd, buf, n, (off_t)off);
  return k > 0 ? (size_t)k : 0;
}

bool platWriteAt(PlatFile* f, int64_t off, const std::string& data) {
  size_t done = 0;
  while (done < data.size()) {
    ssize_t k = pwrite(f->fd, data.data() + done, data.size() - done, (off_t)(off + (int64_t)done));
    if (k <= 0) return false;
    done += (size_t)k;
  }
  return true;
}

bool platTruncate(PlatFile* f, int64_t size) { return ftruncate(f->fd, (off_t)size) == 0; }

bool platFlush(PlatFile* f) { return fsync(f->fd) == 0; }

int64_t platFileSize(const std::wstring& path) {
  struct stat st;
  return stat(posixPath(path).c_str(), &st) == 0 ? (int64_t)st.st_size : -1;
}

bool platMakeDir(const std::wstring& path) {
  const std::string p = posixPath(path);
  return mkdir(p.c_str(), 0700) == 0 || isDirectory(path);
}

bool platCopyFile(const std::wstring& from, const std::wstring& to) {
  std::string data;
  return readFileBytes(from, data) && writeFileAtomic(to, data);
}

bool platCopyNoReplace(const std::wstring& from, const std::wstring& to) {
  struct stat st;
  if (lstat(posixPath(to).c_str(), &st) == 0) return false;
  return platCopyFile(from, to);
}

bool platMoveNoReplace(const std::wstring& from, const std::wstring& to) {
  const std::string a = posixPath(from), b = posixPath(to);
  struct stat st;
  if (lstat(b.c_str(), &st) == 0) return false;
  if (rename(a.c_str(), b.c_str()) == 0) return true;
  if (errno != EXDEV) return false;
  // Another file system (e.g. an SD card): copy, then remove the original only once the copy is whole.
  if (!platCopyFile(from, to)) return false;
  if (unlink(a.c_str()) == 0) return true;
  unlink(b.c_str());
  return false;
}

bool platDeleteFile(const std::wstring& path) { return unlink(posixPath(path).c_str()) == 0; }
bool platRemoveEmptyDir(const std::wstring& path) { return rmdir(posixPath(path).c_str()) == 0; }

std::string platFullPath(const std::string& path) {
  std::string p = path;
  for (auto& c : p)
    if (c == '\\') c = '/';
  char buf[PATH_MAX];
  return realpath(p.c_str(), buf) ? std::string(buf) : p;
}

void platLocalDate(int64_t unixMs, int& year, int& month, int& day) {
  time_t t = (time_t)(unixMs / 1000);
  struct tm lt;
  localtime_r(&t, &lt);
  year = lt.tm_year + 1900;
  month = lt.tm_mon + 1;
  day = lt.tm_mday;
}

uint32_t platProcessId() { return (uint32_t)getpid(); }

void platSleepMs(int ms) { usleep((useconds_t)ms * 1000); }
