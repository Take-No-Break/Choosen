// Export (書き出し): copies or moves the images of one kind into a new folder the user picked, e.g.
// to gather passed or archived images and delete them by hand, or to collect favourites. This is
// the only code that writes into photo folders (a move takes the files out of them), and it runs
// only when the user starts it. Nothing is ever overwritten.
#include "core_internal.h"

namespace core {
namespace {  // nothing here is used by the other core files

struct ExportFile {
  std::wstring from;
  std::string rel;   // path inside the export folder, '\' separated
  int64_t size = 0;  // as scanned: a file of another size at that path is not the rated image
};

// One job at a time. The counters are read by the screens while the job runs.
std::atomic<bool> exportRunning{false};
std::atomic<int> exportDone{0}, exportFailed{0}, exportTotal{0};
std::mutex xmu;  // guards the three fields below
std::string exportWhat, exportFolder;
bool exportMove = false;

// Japanese name of a kind (used in the new folder's name); nullptr if unknown.
const char* exportKindName(const std::string& what) {
  if (what == "archived") return "アーカイブ";
  if (what == "passed") return "見送り";
  if (what == "liked") return "好評と超好評";
  if (what == "likeOnly") return "好評";
  if (what == "super") return "超好評";
  return nullptr;
}

// Ratings are journaled per file, so with work grouping every page carries the work's rating.
bool exportMatches(const std::string& what, const Item& it) {
  if (!it.present) return false;
  if (what == "archived") return it.archived;
  if (it.archived) return false;
  return cardMatches(what, it);
}

// The files of one kind; call with `mu` held. With several photo folders, each one's files go
// into a subfolder named after it, so equal relative paths cannot meet.
std::vector<ExportFile> exportList(const std::string& what, bool move) {
  const auto roots = photoRoots();
  std::vector<std::string> prefix(roots.size());
  if (roots.size() > 1)
    for (size_t r = 0; r < roots.size(); ++r) {
      std::string name;
      for (char c : rootNameOf(roots[r]))
        if (c != ':') name += c;  // "D:\" -> "D"
      if (name.empty()) name = std::to_string(r + 1);
      auto taken = [&](const std::string& n) {
        for (size_t k = 0; k < r; ++k)
          if (toLower(prefix[k]) == toLower(n + "\\")) return true;
        return false;
      };
      std::string unique = name;
      for (int n = 2; taken(unique); ++n) unique = name + " (" + std::to_string(n) + ")";
      prefix[r] = unique + "\\";
    }
  std::vector<ExportFile> out;
  std::unordered_set<uint64_t> picked;
  for (const Item& it : items) {
    if (it.root < 0 || it.root >= (int)roots.size() || !exportMatches(what, it)) continue;
    out.push_back({absPath(it), prefix[it.root] + slashToBack(it.rel), it.size});
    picked.insert(it.fp);
  }
  // A byte-identical copy left behind would bring a moved image back into the library.
  if (move)
    for (const Copy& c : copies)
      if (picked.count(c.fp) && c.root >= 0 && c.root < (int)roots.size())
        out.push_back({widen(rootPath(c.root) + "\\" + slashToBack(c.rel)), prefix[c.root] + slashToBack(c.rel), c.size});
  return out;
}

// Full path, lower case, '\' separated, ending in '\' (compares folders like coreSetConfig).
std::string exportKey(const std::string& path) {
  std::string s = toLower(platFullPath(path));
  std::replace(s.begin(), s.end(), '/', '\\');
  if (s.empty() || s.back() != '\\') s += '\\';
  return s;
}

bool pathExists(const std::string& path) { return platFileSize(longPath(widen(path))) >= 0; }

// Creates a folder and the missing folders above it.
bool makeDirs(const std::string& dir) {
  if (isDirectory(longPath(widen(dir)))) return true;
  const size_t cut = dir.find_last_of("\\/");
  if (cut != std::string::npos && cut > 0) makeDirs(dir.substr(0, cut));
  return platMakeDir(longPath(widen(dir)));
}

// "dir\name.jpg" -> "dir\name (n).jpg"
std::string numberedName(const std::string& path, int n) {
  const size_t slash = path.find_last_of("\\/");
  size_t dot = path.rfind('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot <= slash + 1)) dot = path.size();
  return path.substr(0, dot) + " (" + std::to_string(n) + ")" + path.substr(dot);
}

// Copies or moves one file to `to`, or to "name (2).ext", "name (3).ext", ... when that is taken.
// Returns the path used, "" if it failed.
std::string placeFile(const std::wstring& from, const std::string& to, bool move) {
  for (int n = 1; n < 1000; ++n) {
    const std::string target = n == 1 ? to : numberedName(to, n);
    if (pathExists(target)) continue;
    const std::wstring w = widen(target);
    if (move ? platMoveNoReplace(from, w) : platCopyNoReplace(from, w)) return target;
    if (!pathExists(target)) break;  // failed for another reason (e.g. the original is gone)
  }
  return "";
}

void exportThread(std::vector<ExportFile> files, std::string folder, bool move) {
  const int64_t t0 = nowMs();
  std::unordered_set<std::string> made;  // folders known to exist
  for (const ExportFile& f : files) {
    const std::string to = folder + "\\" + f.rel;
    const std::string dir = to.substr(0, to.find_last_of('\\'));
    // only the file that was rated: one replaced since the last scan stays where it is
    bool ok = platFileSize(longPath(f.from)) == f.size;
    ok = ok && (made.count(dir) || makeDirs(dir));
    if (ok) made.insert(dir);
    ok = ok && !placeFile(f.from, to, move).empty();
    (ok ? exportDone : exportFailed)++;
  }
  logLine("export: " + std::to_string(exportDone.load()) + " files " + (move ? "moved" : "copied") + ", " +
          std::to_string(exportFailed.load()) + " failed, " + std::to_string(nowMs() - t0) + " ms");
  if (move && exportDone > 0) coreStartScan();  // the moved files leave the library; their ratings stay
  exportRunning = false;
}

}  // namespace
}  // namespace core

using namespace core;

std::string coreExportStart(const std::string& what, const std::string& to, bool move) {
  const char* kind = exportKindName(what);
  if (!kind) return "書き出す種類が正しくありません";
  if (to.empty()) return "保存先を選んでください";
  if (!isDirectory(widen(to))) return "保存先のフォルダが見つかりません";
  // Moving rated images out of the library would change the folder ranking; favourites are copied.
  if (move && what != "archived" && what != "passed") return "移動できるのはアーカイブと見送りだけです";
  bool idle = false;
  if (!exportRunning.compare_exchange_strong(idle, true)) return "書き出しの途中です";
  auto refuse = [](const char* why) {
    exportRunning = false;
    return std::string(why);
  };

  std::vector<std::string> roots;
  std::vector<ExportFile> files;
  {
    std::lock_guard<std::mutex> lk(mu);
    roots = photoRoots();
    files = exportList(what, move);
  }
  // Inside a photo folder the exported files would be scanned again as new images.
  const std::string dest = platFullPath(to), key = exportKey(dest);
  for (const auto& r : roots) {
    const std::string rk = exportKey(r);
    if (key.compare(0, rk.size(), rk) == 0) return refuse("写真フォルダの中には書き出せません");
  }
  if (files.empty()) return refuse("該当する画像がありません");

  std::string base = dest;
  while (base.size() > 1 && (base.back() == '\\' || base.back() == '/')) base.pop_back();
  const std::string name = base + "\\Choosen " + kind + " " + localDay(nowMs());
  std::string folder = name;
  for (int n = 2; pathExists(folder); ++n) folder = name + " (" + std::to_string(n) + ")";
  if (!platMakeDir(longPath(widen(folder)))) return refuse("保存先にフォルダを作れません");

  std::string shown = folder;  // in the separators of the platform's own paths
  if (dest.find('\\') == std::string::npos) std::replace(shown.begin(), shown.end(), '\\', '/');
  {
    std::lock_guard<std::mutex> lk(xmu);
    exportWhat = what;
    exportFolder = shown;
    exportMove = move;
    exportDone = 0;
    exportFailed = 0;
    exportTotal = (int)files.size();
  }
  logLine("export: " + std::string(move ? "moving " : "copying ") + std::to_string(files.size()) + " files");
  std::thread(exportThread, std::move(files), folder, move).detach();
  return "";
}

std::string coreExportState() {
  std::lock_guard<std::mutex> lk(xmu);
  std::string o = std::string("{\"running\":") + (exportRunning ? "true" : "false");
  o += ",\"done\":" + std::to_string(exportDone.load());
  o += ",\"failed\":" + std::to_string(exportFailed.load());
  o += ",\"total\":" + std::to_string(exportTotal.load());
  o += std::string(",\"move\":") + (exportMove ? "true" : "false");
  o += ",\"what\":" + jsonStr(exportWhat);
  o += ",\"folder\":" + jsonStr(exportFolder);
  o += "}";
  return o;
}

int coreExportCount(const std::string& what, bool move) {
  if (!exportKindName(what)) return 0;
  std::lock_guard<std::mutex> lk(mu);
  return (int)exportList(what, move).size();
}

std::string coreDebugExportPlace(const std::string& from, const std::string& to, bool move) {
  return placeFile(widen(from), to, move);
}

// ---- Sorting into subfolders (評価ごとにサブフォルダへ) ------------------------------------------
// The owner's own tidying: the pictures of the chosen kinds go into a subfolder named after the kind
// right next to them ("Artist\見送り\x.jpg"), for the folders of a scope or for chosen pictures. The
// files stay in the library (a rating follows the file's content, so it stays too) and the folder
// ranking keeps its artist folders. Pictures already in such a folder stay. The moves of the last
// sort are kept in data\moves.tsv until they are undone ("元に戻す" moves them back). It shares the
// export's job slot (one file job at a time) and its progress counters.
namespace core {
namespace {

const char* const kSortKinds[][2] = {{"passed", "見送り"}, {"likeOnly", "好評"}, {"super", "超好評"}};

const char* sortFolderName(const std::string& kind) {
  for (const auto& k : kSortKinds)
    if (kind == k[0]) return k[1];
  return nullptr;
}

struct SortFile {
  std::wstring from;
  std::string to;    // the new path, in the kind's folder next to it
  int64_t size = 0;  // as scanned: a file of another size there is not the rated image
  std::string kind;
};

std::wstring movesPath() { return g_dataDir + L"\\moves.tsv"; }

std::vector<int> parseIds(const std::string& ids) {
  std::vector<int> out;
  for (const auto& s : split(ids, ','))
    if (!s.empty()) out.push_back(std::atoi(s.c_str()));
  return out;
}

// The files a sort takes; call with `mu` held. kinds: passed, likeOnly, super (comma separated).
std::vector<SortFile> sortList(const std::string& scope, const std::string& ids, const std::string& kinds) {
  std::vector<std::string> want;
  for (const auto& k : split(kinds, ','))
    if (sortFolderName(k)) want.push_back(k);
  std::vector<SortFile> out;
  if (want.empty() || (scope.empty() && ids.empty())) return out;  // never the whole library by accident
  std::vector<char> inScope;
  if (!scope.empty()) inScope = scopeMask(scope);
  std::vector<char> chosen(items.size(), 0);
  for (int id : parseIds(ids))
    if (id >= 0 && id < (int)items.size()) chosen[id] = 1;
  for (size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    const bool picked = !scope.empty() ? (it.folder >= 0 && it.folder < (int)inScope.size() && inScope[it.folder]) : chosen[i];
    if (!picked || !it.present) continue;
    for (const auto& k : want) {
      if (!exportMatches(k, it)) continue;
      const std::string path = absPathOf(it);
      const size_t cut = path.find_last_of("\\/");
      if (cut == std::string::npos) break;
      const std::string dir = path.substr(0, cut);
      const std::string here = dir.substr(dir.find_last_of("\\/") + 1);
      bool sorted = false;  // already in a 見送り / 好評 / 超好評 folder
      for (const auto& s : kSortKinds) sorted |= here == s[1];
      if (!sorted) out.push_back({widen(path), dir + "\\" + sortFolderName(k) + path.substr(cut), it.size, k});
      break;
    }
  }
  return out;
}

// The moves of the last sort (from, to), as written in data\moves.tsv.
std::vector<std::pair<std::string, std::string>> readMoves() {
  std::vector<std::pair<std::string, std::string>> out;
  std::string text;
  if (!readFileBytes(movesPath(), text)) return out;
  for (const auto& line : split(text, '\n')) {
    const auto f = split(line, '\t');
    if (f.size() == 2 && !f[0].empty() && !f[1].empty()) out.push_back({f[0], f[1]});
  }
  return out;
}

void sortThread(std::vector<SortFile> files) {
  const int64_t t0 = nowMs();
  std::unordered_set<std::string> made;
  std::string log;
  for (const SortFile& f : files) {
    const std::string dir = f.to.substr(0, f.to.find_last_of('\\'));
    bool ok = platFileSize(longPath(f.from)) == f.size;
    ok = ok && (made.count(dir) || makeDirs(dir));
    if (ok) made.insert(dir);
    std::string placed;
    if (ok) placed = placeFile(f.from, f.to, true);
    if (!placed.empty()) log += narrow(f.from) + "\t" + placed + "\n";
    (placed.empty() ? exportFailed : exportDone)++;
  }
  if (!log.empty()) writeFileAtomic(movesPath(), log);  // the last sort, for 元に戻す
  logLine("sort: " + std::to_string(exportDone.load()) + " files moved into subfolders, " +
          std::to_string(exportFailed.load()) + " failed, " + std::to_string(nowMs() - t0) + " ms");
  if (exportDone > 0) coreStartScan();  // the files are found in their new folders, ratings and all
  exportRunning = false;
}

void undoThread(std::vector<std::pair<std::string, std::string>> moves) {
  std::unordered_set<std::string> made;
  for (auto m = moves.rbegin(); m != moves.rend(); ++m) {
    const std::string dir = m->first.substr(0, m->first.find_last_of("\\/"));
    bool ok = made.count(dir) || makeDirs(dir);
    if (ok) made.insert(dir);
    ok = ok && !placeFile(widen(m->second), m->first, true).empty();
    (ok ? exportDone : exportFailed)++;
  }
  // the kind folders the sort made go too, once nothing is left in them (anything else keeps them)
  std::unordered_set<std::string> kindDirs;
  for (const auto& m : moves) kindDirs.insert(m.second.substr(0, m.second.find_last_of("\\/")));
  for (const auto& d : kindDirs) platRemoveEmptyDir(widen(d));
  platDeleteFile(movesPath());
  logLine("sort undone: " + std::to_string(exportDone.load()) + " files back, " + std::to_string(exportFailed.load()) +
          " failed");
  if (exportDone > 0) coreStartScan();
  exportRunning = false;
}

bool startJob(const char* what, int total) {
  bool idle = false;
  if (!exportRunning.compare_exchange_strong(idle, true)) return false;
  std::lock_guard<std::mutex> lk(xmu);
  exportWhat = what;
  exportFolder.clear();
  exportMove = true;
  exportDone = 0;
  exportFailed = 0;
  exportTotal = total;
  return true;
}

}  // namespace
}  // namespace core

std::string coreSortCount(const std::string& scope, const std::string& ids, const std::string& kinds) {
  int n[3] = {0, 0, 0};
  {
    std::lock_guard<std::mutex> lk(mu);
    for (const auto& f : sortList(scope, ids, kinds))
      for (int k = 0; k < 3; ++k)
        if (f.kind == kSortKinds[k][0]) n[k]++;
  }
  return "{\"passed\":" + std::to_string(n[0]) + ",\"likeOnly\":" + std::to_string(n[1]) + ",\"super\":" +
         std::to_string(n[2]) + ",\"undo\":" + std::to_string(readMoves().size()) + "}";
}

std::string coreSortStart(const std::string& scope, const std::string& ids, const std::string& kinds) {
  if (scope.empty() && ids.empty()) return "フォルダか画像を選んでください";
  std::vector<SortFile> files;
  {
    std::lock_guard<std::mutex> lk(mu);
    files = sortList(scope, ids, kinds);
  }
  if (files.empty()) return "該当する画像がありません";
  if (!startJob("sort", (int)files.size())) return "ほかのファイル操作の途中です";
  logLine("sort: moving " + std::to_string(files.size()) + " files into subfolders");
  std::thread(sortThread, std::move(files)).detach();
  return "";
}

std::string coreSortUndo() {
  auto moves = readMoves();
  if (moves.empty()) return "元に戻す移動がありません";
  if (!startJob("undo", (int)moves.size())) return "ほかのファイル操作の途中です";
  std::thread(undoThread, std::move(moves)).detach();
  return "";
}

std::string coreSortMoves() {
  std::string o = "{\"moves\":[";
  bool first = true;
  for (const auto& m : readMoves()) {
    o += std::string(first ? "" : ",") + "[" + jsonStr(m.first) + "," + jsonStr(m.second) + "]";
    first = false;
  }
  return o + "]}";
}
