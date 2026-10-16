// The image library: photo folders, the folder tree, Pixiv works, the index cache and scanning.
#include "core_internal.h"

namespace core {

std::wstring journalPath() { return g_dataDir + L"\\ratings.log"; }

// index.tsv (v1) lists the first photo folder only and is what exes before v0.6 read; it is still
// written so that swapping an older exe back in finds a current list. index2.tsv (v2) lists every
// photo folder.
std::wstring indexPath() { return g_dataDir + L"\\index.tsv"; }

std::wstring index2Path() { return g_dataDir + L"\\index2.tsv"; }

// Photo folders in priority order: when the same file is in two of them, the earlier one wins.
std::vector<std::string> photoRoots() {
  std::vector<std::string> r;
  if (g_cfg.root.empty()) return r;
  r.push_back(g_cfg.root);
  for (auto& m : g_cfg.moreRoots)
    if (!m.empty()) r.push_back(m);
  return r;
}

// Subfolder levels read in a photo folder: -1 = all, 0 = only the images directly in it.
int maxDepthOf(int root) {
  return root >= 0 && root < (int)g_cfg.maxDepths.size() ? g_cfg.maxDepths[root] : -1;
}

// Artist level of a photo folder: the artist depth, or the deepest level read when that is less
// (a photo folder read without subfolders is one artist itself).
// A photo folder without subfolders (all its images directly in it) is one artist itself, named
// after the folder; otherwise the artist level is never deeper than the deepest folder with images.
int artistLevelOf(int root) {
  int md = maxDepthOf(root);
  int level = md >= 0 ? std::min(md, g_cfg.artistDepth) : g_cfg.artistDepth;
  if (root >= 0 && root < (int)rootDeepest.size()) level = std::min(level, rootDeepest[root]);
  return level;
}

bool isArtist(const Folder& f) { return f.depth == artistLevelOf(f.root); }

std::string rootPath(int root) {
  if (root == 0) return g_cfg.root;
  int k = 0;
  for (auto& m : g_cfg.moreRoots)
    if (!m.empty() && ++k == root) return m;
  return std::string();
}

std::string slashToBack(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

std::string absPathOf(const Item& it) { return rootPath(it.root) + "\\" + slashToBack(it.rel); }

std::wstring absPath(const Item& it) { return widen(absPathOf(it)); }

// Splits a journal path into photo folder and relative path; unknown folders keep the full path.
std::string relFromAbs(const std::string& abs, int& rootOut) {
  auto roots = photoRoots();
  for (int i = 0; i < (int)roots.size(); ++i) {
    std::string root = toLower(roots[i]) + "\\";
    if (abs.size() > root.size() && toLower(abs.substr(0, root.size())) == root) {
      std::string r = abs.substr(root.size());
      std::replace(r.begin(), r.end(), '\\', '/');
      rootOut = i;
      return r;
    }
  }
  rootOut = 0;
  return abs;
}

std::string extOf(const std::string& rel) {
  size_t d = rel.rfind('.');
  size_t s = rel.rfind('/');
  if (d == std::string::npos || (s != std::string::npos && d < s)) return "";
  return toLower(rel.substr(d + 1));
}

// Folder tree: every photo folder is a top node (depth 0), so artist depth counts from each photo
// folder. The first photo folder keeps the paths single-folder versions used ("" for itself,
// "Artist/sub" below it), so saved scopes and gallery conditions still select the same folders.
// A further photo folder's node is keyed by its full path ("D:\More", "D:\More/Artist"); relative
// paths use '/' and never contain ':' or '\', so the two kinds cannot collide.
bool isTopKey(const std::string& path) {
  return !path.empty() && path.find('/') == std::string::npos && path.find_first_of(":\\") != std::string::npos;
}

// Folder path of an image: its directory, prefixed with the key of a further photo folder.
std::string folderKeyOf(const Item& it) {
  size_t s = it.rel.rfind('/');
  std::string dir = s == std::string::npos ? std::string() : it.rel.substr(0, s);
  if (it.root == 0) return dir;
  std::string top = rootPath(it.root);
  return dir.empty() ? top : top + "/" + dir;
}

int ensureFolder(const std::string& path) {
  auto f = folderByPath.find(path);
  if (f != folderByPath.end()) return f->second;
  Folder fo;
  fo.path = path;
  if (path.empty()) {
    fo.name = rootName;
  } else if (isTopKey(path)) {
    fo.name = rootNameOf(path);
    const auto roots = photoRoots();
    for (int r = 1; r < (int)roots.size(); ++r)
      if (roots[r] == path) fo.root = r;
  } else {
    size_t s = path.rfind('/');
    int parent = ensureFolder(s == std::string::npos ? std::string() : path.substr(0, s));
    fo.parent = parent;
    fo.root = folders[parent].root;
    fo.depth = folders[parent].depth + 1;
    fo.name = s == std::string::npos ? path : path.substr(s + 1);
  }
  int id = (int)folders.size();
  folders.push_back(std::move(fo));
  folderByPath[path] = id;
  return id;
}

// PixivUtil2 names pages "<illust id>[-<32 hex>]_p<page>[ - title].<ext>". Returns the work key.
bool pixivPage(const std::string& name, std::string& key, int& page) {
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  auto hex = [&](char c) { return digit(c) || (c >= 'a' && c <= 'f'); };
  size_t i = 0;
  while (i < name.size() && digit(name[i])) i++;
  if (i == 0) return false;
  if (i < name.size() && name[i] == '-') {
    size_t h = i + 1, k = h;
    while (k < name.size() && hex(name[k])) k++;
    if (k - h != 32) return false;
    i = k;
  }
  if (i + 2 >= name.size() || name[i] != '_' || name[i + 1] != 'p' || !digit(name[i + 2])) return false;
  size_t k = i + 2;
  int pg = 0;
  while (k < name.size() && digit(name[k]) && pg < 100000) pg = pg * 10 + (name[k++] - '0');
  if (k < name.size() && name[k] != ' ' && name[k] != '.') return false;
  key = name.substr(0, i);
  page = pg;
  return true;
}

void rebuildWorks() {
  works.clear();
  std::unordered_map<std::string, int> byKey;
  std::unordered_set<std::string> keys;
  groupedCount = 0;
  for (int i = 0; i < (int)items.size(); ++i) {
    Item& it = items[i];
    it.work = -1;
    it.page = 0;
    if (!it.present || it.folder < 0 || it.archived) continue;  // archived images form no card
    size_t s = it.rel.rfind('/');
    std::string name = s == std::string::npos ? it.rel : it.rel.substr(s + 1), key;
    int page = 0;
    int w;
    const bool pix = pixivPage(name, key, page);
    if (!pix || keys.insert(std::to_string(it.folder) + "|" + key).second) groupedCount++;
    if (g_cfg.groupWorks && pix) {
      auto f = byKey.find(std::to_string(it.folder) + "|" + key);
      if (f == byKey.end()) {
        w = (int)works.size();
        works.push_back({it.folder, {}});
        byKey[std::to_string(it.folder) + "|" + key] = w;
      } else {
        w = f->second;
      }
      it.page = page;
    } else {
      w = (int)works.size();
      works.push_back({it.folder, {}});
    }
    it.work = w;
    works[w].pages.push_back(i);
  }
  for (auto& w : works)
    std::sort(w.pages.begin(), w.pages.end(), [](int a, int b) {
      if (items[a].page != items[b].page) return items[a].page < items[b].page;
      return items[a].rel < items[b].rel;
    });
}

// Parents are always created before children, so a folder's id is larger than its parent's.
void rebuildFolders() {
  folders.clear();
  folderByPath.clear();
  ensureFolder("");
  for (auto& it : items) {
    if (!it.present) {
      it.folder = -1;
      continue;
    }
    it.folder = ensureFolder(folderKeyOf(it));
  }
  rootDeepest.assign(photoRoots().size(), 0);
  for (const Folder& f : folders)
    if (f.root >= 0 && f.root < (int)rootDeepest.size()) rootDeepest[f.root] = std::max(rootDeepest[f.root], f.depth);
  rebuildWorks();
  computeFolders();
  baselineDirty = true;
}

// scope: folder paths separated by '|' (each with its subfolders); "" = everything. A path with a
// trailing '/' means only the images directly in that folder: "/" for the first photo folder (the
// gallery's tile for them), "D:\More/" for a further one. "/*" = the whole first photo folder
// (its own path "" alone would mean everything).
std::vector<char> scopeMask(const std::string& scope) {
  std::vector<char> m(folders.size(), scope.empty() ? 1 : 0);
  if (scope.empty()) return m;
  std::vector<char> marked(folders.size(), 0), direct(folders.size(), 0);
  for (auto p : split(scope, '|')) {
    bool only = false;
    if (p == "/*") {
      p.clear();
    } else if (!p.empty() && p.back() == '/') {
      p.pop_back();
      only = true;
    } else if (p.empty()) {
      continue;
    }
    auto f = folderByPath.find(p);
    if (f != folderByPath.end()) (only ? direct : marked)[f->second] = 1;
  }
  std::vector<char> tree(folders.size(), 0);  // inside a folder chosen with its subfolders
  for (size_t i = 0; i < folders.size(); ++i) {
    int par = folders[i].parent;
    tree[i] = marked[i] || (par >= 0 && tree[par]);
    m[i] = tree[i] || direct[i];
  }
  return m;
}

std::string baseName(const std::string& rel) {
  size_t s = rel.rfind('/');
  return s == std::string::npos ? rel : rel.substr(s + 1);
}

// Adds a file from the index cache. The same content in two photo folders is one image, kept in
// the earlier folder.
void addIndexed(const std::vector<std::string>& f, size_t relCol, int root) {
  if (f.size() <= relCol || f[relCol].empty()) return;
  uint64_t fp = parseHex64(f[0]);
  if (!fp) return;
  auto known = byFp.find(fp);
  if (known != byFp.end() && items[known->second].root <= root) return;
  Item& it = known != byFp.end() ? items[known->second] : items.emplace_back();
  if (known == byFp.end()) byFp[fp] = (int)items.size() - 1;
  it.fp = fp;
  it.size = std::strtoll(f[1].c_str(), nullptr, 10);
  it.mtime = std::strtoull(f[2].c_str(), nullptr, 10);
  it.rel = f[relCol];
  it.root = root;
  it.present = true;
}

// v2 header: "#choosen-index-2 <stamp>", then "#root <n> <path>" per photo folder; lines are
// "fp size mtime n rel". v1 header: "#choosen-index-1 <first photo folder> [<stamp>]"; lines are
// "fp size mtime rel". Both files are written together with the same stamp. A v1 file without that
// stamp was rewritten by an older exe (or no v2 file exists yet), so its entries for the first
// photo folder are the newer ones. Folders are matched by path, so a v2 file stays usable when
// photo folders are added, removed or reordered; files of unknown folders are simply not loaded.
void loadIndex() {
  const auto roots = photoRoots();
  std::string s1, s2;
  std::vector<std::string> l1, l2, h1, h2;
  if (readFileBytes(indexPath(), s1)) l1 = split(s1, '\n');
  if (readFileBytes(index2Path(), s2)) l2 = split(s2, '\n');
  if (!l1.empty()) h1 = split(l1[0], '\t');
  if (!l2.empty()) h2 = split(l2[0], '\t');
  const bool v1ok = h1.size() >= 2 && h1[0] == "#choosen-index-1" && h1[1] == g_cfg.root;
  const bool v2ok = h2.size() >= 2 && h2[0] == "#choosen-index-2";
  const bool v1newer = v1ok && (!v2ok || h1.size() < 3 || h1[2] != h2[1]);
  if (v2ok) {
    std::vector<int> rootMap;  // folder number in the file -> index in photoRoots(), -1 = unknown
    size_t i = 1;
    for (; i < l2.size() && !l2[i].empty() && l2[i][0] == '#'; ++i) {
      auto f = split(l2[i], '\t');
      if (f.size() < 3 || f[0] != "#root") continue;
      int n = atoi(f[1].c_str());
      if (n < 0 || n > 1000) continue;
      if ((int)rootMap.size() <= n) rootMap.resize(n + 1, -1);
      for (int r = 0; r < (int)roots.size(); ++r)
        if (toLower(roots[r]) == toLower(f[2])) rootMap[n] = r;
    }
    for (; i < l2.size(); ++i) {
      auto f = split(l2[i], '\t');
      if (f.size() < 5) continue;
      int n = atoi(f[3].c_str());
      if (n < 0 || n >= (int)rootMap.size() || rootMap[n] < 0) continue;
      if (f.size() >= 6 && f[5] == "copy") {  // checked by the scan whenever the file changed
        Copy c;
        c.fp = parseHex64(f[0]);
        c.size = std::strtoll(f[1].c_str(), nullptr, 10);
        c.mtime = std::strtoull(f[2].c_str(), nullptr, 10);
        c.root = rootMap[n];
        c.rel = f[4];
        if (c.fp && !c.rel.empty()) copies.push_back(std::move(c));
        continue;
      }
      if (v1newer && rootMap[n] == 0) continue;
      addIndexed(f, 4, rootMap[n]);
    }
  }
  if (v1newer)
    for (size_t i = 1; i < l1.size(); ++i) addIndexed(split(l1[i], '\t'), 3, 0);
}

bool isImageName(const std::wstring& n) {
  size_t d = n.rfind(L'.');
  if (d == std::wstring::npos) return false;
  std::string e = toLower(narrow(n.substr(d + 1)));
  static const char* exts[] = {"jpg", "jpeg", "jfif", "png", "gif", "webp", "avif",
                               "bmp", "tif", "tiff", "heic", "heif"};
  for (auto x : exts)
    if (e == x) return true;
  return false;
}

bool ignoredDir(const DirEntry& e, const std::vector<std::string>& ign) {
  if (e.hidden) return true;
  std::string n = toLower(narrow(e.name));
  for (auto& p : ign) {
    if (p == ".*") {
      if (!n.empty() && n[0] == '.') return true;
    } else if (!p.empty() && n == p) {
      return true;
    }
  }
  return false;
}

// levels: subfolder levels still to read below relDir (-1 = all).
void enumerate(const std::wstring& root, const std::string& relDir, std::vector<Found>& out,
               const std::vector<std::string>& ign, int gen, int levels) {
  if (scanGen != gen) return;
  std::wstring dir = relDir.empty() ? root : root + L"\\" + widen(slashToBack(relDir));
  std::vector<DirEntry> entries;
  if (!platListDir(dir, entries)) return;
  std::vector<std::string> subdirs;
  for (auto& e : entries) {
    if (e.dir) {
      if (!ignoredDir(e, ign)) subdirs.push_back(narrow(e.name));
      continue;
    }
    if (!isImageName(e.name)) continue;
    Found f;
    f.rel = relDir.empty() ? narrow(e.name) : relDir + "/" + narrow(e.name);
    f.size = e.size;
    f.mtime = e.mtime;
    f.fp = 0;
    out.push_back(std::move(f));
    scanFound++;
  }
  if (levels == 0) return;
  for (auto& s : subdirs) enumerate(root, relDir.empty() ? s : relDir + "/" + s, out, ign, gen, levels < 0 ? -1 : levels - 1);
}

// Identity of a file = size + first 8 KB + last 8 KB. Survives renames and moves.
uint64_t fingerprint(const std::wstring& path, int64_t size) {
  PlatFile* h = platOpen(path, PlatOpen::Read);
  if (!h) return 0;
  const size_t kChunk = 8192;
  uint8_t buf[8192];
  uint64_t hs = 1469598103934665603ULL ^ mix64((uint64_t)size);
  auto feed = [&](int64_t off) {
    const size_t got = platReadAt(h, off, buf, kChunk);
    for (size_t i = 0; i < got; ++i) {
      hs ^= buf[i];
      hs *= 1099511628211ULL;
    }
  };
  feed(0);
  if (size > (int64_t)kChunk) feed(std::max<int64_t>(kChunk, size - kChunk));
  platClose(h);
  uint64_t r = mix64(hs);
  return r ? r : 1;
}

// 1 = the two files hold the same bytes, 0 = they differ, -1 = one cannot be read.
int sameContent(const std::wstring& a, const std::wstring& b) {
  PlatFile* x = platOpen(a, PlatOpen::Read);
  PlatFile* y = platOpen(b, PlatOpen::Read);
  int r = -1;
  if (x && y) {
    r = platSize(x) == platSize(y) ? 1 : 0;
    std::vector<char> bx(1 << 16), by(1 << 16);
    for (int64_t off = 0; r == 1;) {
      const size_t gx = platReadAt(x, off, bx.data(), bx.size()), gy = platReadAt(y, off, by.data(), by.size());
      if (gx != gy || std::memcmp(bx.data(), by.data(), gx) != 0) r = 0;
      if (gx == 0) break;
      off += (int64_t)gx;
    }
  }
  platClose(x);
  platClose(y);
  return r;
}

// Id of an image whose fingerprint (size + first and last 8 KB) is already taken by a different
// image: a hash of the whole file, so it is the same on every scan and on every device.
uint64_t contentId(const std::wstring& path, int64_t size) {
  PlatFile* h = platOpen(path, PlatOpen::Read);
  if (!h) return 0;
  uint64_t hs = 0xcbf29ce484222325ULL ^ mix64((uint64_t)size ^ 0x436f6e74656e7431ULL);
  std::vector<uint8_t> buf(1 << 16);
  for (int64_t off = 0;;) {
    const size_t got = platReadAt(h, off, buf.data(), buf.size());
    if (!got) break;
    for (size_t i = 0; i < got; ++i) {
      hs ^= buf[i];
      hs *= 1099511628211ULL;
    }
    off += (int64_t)got;
  }
  platClose(h);
  uint64_t r = mix64(hs ^ 0x9E3779B97F4A7C15ULL);
  return r ? r : 2;
}

// Adds hashed files. A file whose fingerprint belongs to an image that is somewhere else (found
// by this scan, or still at its known place) is not taken over: it goes to `check`, and
// checkCopies compares the bytes outside the lock. So which file keeps an image (and its
// ratings) never depends on the order the files were hashed in.
void mergeFound(std::vector<Found>& batch, int gen, std::vector<Found>& check) {
  std::lock_guard<std::mutex> lk(mu);
  for (auto& f : batch) {
    if (!f.fp) continue;
    auto it = byFp.find(f.fp);
    if (it != byFp.end()) {
      Item& item = items[it->second];
      const bool elsewhere = item.rel != f.rel || item.root != f.root;
      if (elsewhere && !f.force && !item.rel.empty() &&
          ((item.seenGen == gen && item.present) || platFileSize(absPath(item)) == f.size)) {
        check.push_back(f);
        continue;
      }
      item.rel = f.rel;
      item.root = f.root;
      item.size = f.size;
      item.mtime = f.mtime;
      item.present = true;
      item.seenGen = gen;
    } else {
      Item item;
      item.fp = f.fp;
      item.rel = f.rel;
      item.root = f.root;
      item.size = f.size;
      item.mtime = f.mtime;
      item.present = true;
      item.seenGen = gen;
      computeItem(item);
      byFp[f.fp] = (int)items.size();
      items.push_back(std::move(item));
    }
  }
  rebuildFolders();
  version++;
}

// Files whose fingerprint belongs to another file. The same bytes make a copy: one image, and the
// earlier photo folder holds it. Different bytes make a different image, which gets contentId.
// If the other file changed meanwhile (it no longer has that fingerprint), this one carries the
// image on. Unreadable files are left for the next scan.
void checkCopies(std::vector<Found> check, int gen, const std::vector<std::wstring>& roots) {
  for (int round = 0; !check.empty() && round < 4; ++round) {  // a new id can meet a copy of itself
    std::vector<Found> merge, next;
    for (Found f : check) {
      std::wstring home;
      {
        std::lock_guard<std::mutex> lk(mu);
        auto it = byFp.find(f.fp);
        if (it != byFp.end()) home = absPath(items[it->second]);
      }
      const std::wstring path = roots[f.root] + L"\\" + widen(slashToBack(f.rel));
      const int same = home.empty() ? 0 : sameContent(path, home);
      if (same < 0) continue;
      if (same == 0) {
        if (home.empty() || fingerprint(home, f.size) != f.fp) {
          f.force = true;  // the image's old file is gone or changed
        } else {
          f.fp = contentId(path, f.size);
          if (!f.fp) continue;
          scanSplit++;
        }
        merge.push_back(f);
        continue;
      }
      std::lock_guard<std::mutex> lk(mu);
      auto it = byFp.find(f.fp);
      if (it == byFp.end()) continue;
      Item& o = items[it->second];
      Copy c;
      c.fp = f.fp;
      c.seenGen = gen;
      if (f.root < o.root) {  // the earlier photo folder holds the image
        c.root = o.root, c.rel = o.rel, c.size = o.size, c.mtime = o.mtime;
        o.root = f.root, o.rel = f.rel, o.size = f.size, o.mtime = f.mtime;
        o.present = true;
        o.seenGen = gen;
      } else {
        c.root = f.root, c.rel = f.rel, c.size = f.size, c.mtime = f.mtime;
      }
#if !defined(__APPLE__)  // on iPhone a picture in two albums is normal: no album names in the log
      if (scanDupes++ < 50) logLine("copy: " + c.rel + "  ==  " + o.rel);
#else
      scanDupes++;
#endif
      copies.push_back(std::move(c));
    }
    mergeFound(merge, gen, next);
    check.swap(next);
  }
  std::lock_guard<std::mutex> lk(mu);
  rebuildFolders();
  version++;
}

// A copy whose image was not found by this scan (its file was deleted or changed) becomes that
// image, so the ratings stay with the pictures that are still there.
void promoteCopies(int gen, const std::vector<std::wstring>& roots) {
  std::vector<Found> promote;
  {
    std::lock_guard<std::mutex> lk(mu);
    std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) {
      return a.root != b.root ? a.root < b.root : a.rel < b.rel;
    });
    std::unordered_set<uint64_t> taken;
    for (size_t i = 0; i < copies.size();) {
      const Copy& c = copies[i];
      auto it = byFp.find(c.fp);
      const bool imageHere = it != byFp.end() && items[it->second].present && items[it->second].seenGen == gen;
      if (c.seenGen == gen && !imageHere && taken.insert(c.fp).second) {
        Found f;
        f.root = c.root, f.rel = c.rel, f.size = c.size, f.mtime = c.mtime, f.fp = c.fp, f.force = true;
        promote.push_back(f);
        copies.erase(copies.begin() + (ptrdiff_t)i);
        continue;
      }
      ++i;
    }
  }
  if (promote.empty()) return;
  std::vector<Found> check;
  mergeFound(promote, gen, check);
  checkCopies(check, gen, roots);
}

void scanThread(int gen) {
  std::vector<std::wstring> roots;
  std::vector<int> levels;
  std::vector<std::string> ign;
  {
    std::lock_guard<std::mutex> lk(mu);
    for (auto& r : photoRoots()) {
      levels.push_back(maxDepthOf((int)roots.size()));
      roots.push_back(widen(r));
    }
    for (auto& p : split(toLower(g_cfg.ignore), ';'))
      if (!p.empty()) ign.push_back(p);
    scanError.clear();
  }
  scanning = true;
  scanFound = scanHashed = scanToHash = scanDupes = scanSplit = 0;
  version++;
  int64_t t0 = nowMs();

  // A photo folder that is missing (e.g. its drive is not connected) is skipped: its images keep
  // their current state instead of disappearing.
  std::vector<char> scanned(roots.size(), 0);
  std::vector<Found> found;
  for (int r = 0; r < (int)roots.size(); ++r) {
    if (!isDirectory(roots[r])) continue;
    scanned[r] = 1;
    size_t from = found.size();
    enumerate(roots[r], "", found, ign, gen, levels[r]);
    for (size_t k = from; k < found.size(); ++k) found[k].root = r;
    if (scanGen != gen) return;
  }
  if (std::count(scanned.begin(), scanned.end(), 1) < (int)roots.size()) {
    std::lock_guard<std::mutex> lk(mu);
    scanError = "フォルダが見つかりません";
    if (std::count(scanned.begin(), scanned.end(), 1) == 0) {
      scanning = false;
      version++;
      return;
    }
  }

  std::vector<Found> toHash;
  {
    std::lock_guard<std::mutex> lk(mu);
    auto relKey = [](int root, const std::string& rel) { return std::to_string(root) + "\t" + rel; };
    std::unordered_map<std::string, int> byRel;
    for (int i = 0; i < (int)items.size(); ++i)
      if (!items[i].rel.empty()) byRel[relKey(items[i].root, items[i].rel)] = i;
    std::unordered_map<std::string, size_t> copyByRel;
    for (size_t i = 0; i < copies.size(); ++i) copyByRel[relKey(copies[i].root, copies[i].rel)] = i;
    for (auto& f : found) {
      auto c = copyByRel.find(relKey(f.root, f.rel));
      if (c != copyByRel.end() && copies[c->second].size == f.size && copies[c->second].mtime == f.mtime) {
        copies[c->second].seenGen = gen;  // a known copy, unchanged: no need to read it again
        continue;
      }
      auto r = byRel.find(relKey(f.root, f.rel));
      if (r != byRel.end()) {
        Item& it = items[r->second];
        if (it.size == f.size && it.mtime == f.mtime && it.fp) {
          it.present = true;
          it.seenGen = gen;
          continue;
        }
      }
      toHash.push_back(f);
    }
    for (auto& it : items)
      if (it.seenGen != gen && (it.root >= (int)scanned.size() || scanned[it.root])) it.present = false;
    // copies that are gone or changed (a changed one is fingerprinted again above)
    copies.erase(std::remove_if(copies.begin(), copies.end(), [&](const Copy& c) {
                   return c.seenGen != gen && (c.root >= (int)scanned.size() || scanned[c.root]);
                 }), copies.end());
    rebuildFolders();
    version++;
  }
  found.clear();
  found.shrink_to_fit();

  scanToHash = (int)toHash.size();
  std::atomic<size_t> next{0};
  std::mutex rmu;
  std::vector<Found> results;
  auto worker = [&] {
    for (;;) {
      size_t i = next++;
      if (i >= toHash.size() || scanGen != gen) break;
      Found f = toHash[i];
      f.fp = fingerprint(roots[f.root] + L"\\" + widen(slashToBack(f.rel)), f.size);
      {
        std::lock_guard<std::mutex> lk(rmu);
        results.push_back(std::move(f));
      }
      scanHashed++;
    }
  };
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i) workers.emplace_back(worker);
  std::vector<Found> check;  // files that share a fingerprint with another file
  int64_t lastMerge = nowMs(), lastSave = nowMs();
  for (;;) {
    bool done = scanHashed >= scanToHash || scanGen != gen;
    if (!done) platSleepMs(100);
    if (done || nowMs() - lastMerge > 1000) {
      std::vector<Found> b;
      {
        std::lock_guard<std::mutex> lk(rmu);
        b.swap(results);
      }
      if (!b.empty() && scanGen == gen) mergeFound(b, gen, check);
      lastMerge = nowMs();
    }
    if (nowMs() - lastSave > 30000) {
      coreSaveIndex();
      lastSave = nowMs();
    }
    if (done) break;
  }
  for (auto& w : workers) w.join();
  if (scanGen != gen) return;
  {
    std::vector<Found> b;
    b.swap(results);
    if (!b.empty()) mergeFound(b, gen, check);
  }
  checkCopies(std::move(check), gen, roots);
  promoteCopies(gen, roots);
  coreSaveIndex();
  logLine("scan done: " + std::to_string(scanFound.load()) + " files, " + std::to_string(scanToHash.load()) +
          " hashed, " + std::to_string(scanDupes.load()) + " duplicates, " +
          std::to_string(scanSplit.load()) + " different images sharing a fingerprint, " + std::to_string(nowMs() - t0) + " ms");
  scanning = false;
  version++;
}

std::string rootNameOf(const std::string& root) {
  std::string r = root;
  while (!r.empty() && (r.back() == '\\' || r.back() == '/')) r.pop_back();
  size_t s = r.find_last_of("\\/");
  return s == std::string::npos ? r : r.substr(s + 1);
}

// Artist-level folder of every folder (the folder itself when it is not deeper than that).
std::vector<int> artistFolders() {
  std::vector<int> artist(folders.size());
  for (size_t f = 0; f < folders.size(); ++f) {
    int a = (int)f;
    while (a >= 0 && folders[a].depth > g_cfg.artistDepth) a = folders[a].parent;
    artist[f] = a;
  }
  return artist;
}

// Archived images are not part of any work, so the archive view groups them itself (by Pixiv
// work when grouping is on) to show and restore a whole work as one card.
std::vector<std::vector<int>> archivedGroups(const std::vector<char>& inScope) {
  std::vector<std::vector<int>> groups;
  std::unordered_map<std::string, size_t> byKey;
  for (int i = 0; i < (int)items.size(); ++i) {
    Item& it = items[i];
    if (!it.present || !it.archived || it.folder < 0 || !inScope[it.folder]) continue;
    std::string key;
    int page = 0;
    if (g_cfg.groupWorks && pixivPage(baseName(it.rel), key, page)) {
      it.page = page;
      auto f = byKey.find(std::to_string(it.folder) + "|" + key);
      if (f != byKey.end()) {
        groups[f->second].push_back(i);
        continue;
      }
      byKey[std::to_string(it.folder) + "|" + key] = groups.size();
    }
    groups.push_back({i});
  }
  for (auto& g : groups)
    std::sort(g.begin(), g.end(), [](int a, int b) {
      if (items[a].page != items[b].page) return items[a].page < items[b].page;
      return items[a].rel < items[b].rel;
    });
  return groups;
}

}  // namespace core

