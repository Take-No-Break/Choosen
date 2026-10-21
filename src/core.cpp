// Shared state, settings, start-up and the developer hooks.
#include "core_internal.h"

namespace core {

Policy g_policy;

std::mutex mu;

std::mutex jmu;  // orders journal appends; always taken while holding mu, released after the write
std::mutex rateMu;
void (*g_journalHook)() = nullptr;

std::unordered_map<std::string, std::vector<CidRef>> cidMap;  // one rating = one event per page
std::map<std::pair<uint64_t, uint64_t>, uint64_t> replacedBy;

uint64_t currentTick(const Item& it, uint64_t t) {
  for (int hop = 0; hop < 1000; ++hop) {  // a chain of corrections (bounded: ticks only grow)
    for (const Event& e : it.ev)
      if (e.tick == t) return t;
    auto r = replacedBy.find({it.fp, t});
    if (r == replacedBy.end() || r->second <= t) return kNoTick;
    t = r->second;
  }
  return kNoTick;
}

std::unordered_set<std::string> pendingCids;                  // ratings being written right now

std::string backupWarning;

std::vector<Item> items;

std::vector<Work> works;

std::unordered_map<uint64_t, int> byFp;

std::vector<Folder> folders;

std::unordered_map<std::string, int> folderByPath;

uint64_t tick = 0;

int sessionSwipes = 0;

uint64_t sessionSeed = 0;

std::string rootName;

std::atomic<uint64_t> version{1};

std::atomic<bool> scanning{false};

std::atomic<int> scanGen{0}, scanFound{0}, scanHashed{0}, scanToHash{0}, scanDupes{0}, scanSplit{0};
// scanDupes: copies found by the last scan; scanSplit: different images that shared a fingerprint
std::vector<Copy> copies;
std::vector<int> rootDeepest;

std::string scanError;

uint64_t sessionTick0 = 0;  // first tick of this session; ranks before it are the baseline

bool baselineDirty = true;

int groupedCount = 0;  // how many cards there would be with grouping on (shown in settings)

std::atomic<int> lastBackupDay{0};

// Lines with keys this version does not know (written by a newer version): kept and written
// back unchanged, so going back to an older exe and saving does not lose newer settings.
std::vector<std::string> g_unknownConfig;

}  // namespace core

using namespace core;

Config g_cfg;

bool g_journalFlush = true;

// Custom key / mouse bindings are short "name:value,value;..." lists of key codes and names.
bool validBindings(const std::string& s) {
  if (s.size() > 2000) return false;
  for (char c : s)
    if (!isalnum((unsigned char)c) && c != '_' && c != ':' && c != ',' && c != ';') return false;
  return true;
}

int effectiveThrowMs() { return g_cfg.advanced ? g_cfg.throwDelayMs : g_cfg.throwSlow ? 500 : 0; }

int effectiveHoldMs() { return g_cfg.advanced ? g_cfg.longPressMs : 400; }

void loadConfig() {
  std::string s;
  if (!readFileBytes(g_dataDir + L"\\config.txt", s)) return;
  bool sawThrowMode = false, sawThrowDelay = false;
  std::map<int, std::string> more;  // root2=, root3=, ... (numbered keys: folder names may contain ';')
  std::map<int, int> depths;        // root_max_depth= (folder 1), root2_max_depth=, ...
  g_unknownConfig.clear();
  for (auto line : split(s, '\n')) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t eq = line.find('=');
    if (eq == std::string::npos || line[0] == '#') continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "root") g_cfg.root = v;
    else if (k.size() > 4 && k.size() < 8 && k.compare(0, 4, "root") == 0 &&
             k.find_first_not_of("0123456789", 4) == std::string::npos && atoi(k.c_str() + 4) >= 2)
      more[atoi(k.c_str() + 4)] = v;
    else if (k.size() >= 14 && k.compare(0, 4, "root") == 0 && k.compare(k.size() - 10, 10, "_max_depth") == 0 &&
             k.find_first_not_of("0123456789", 4) == k.size() - 10) {
      int n = k.size() == 14 ? 1 : atoi(k.c_str() + 4);
      if (n >= 1 && n < 1000) depths[n] = std::clamp(atoi(v.c_str()), -1, kMaxSubfolderDepth);
    } else if (k == "artist_depth") g_cfg.artistDepth = std::max(1, atoi(v.c_str()));
    else if (k == "mouse_mode") g_cfg.mouseMode = std::clamp(atoi(v.c_str()), 1, 4);
    else if (k == "port") g_cfg.port = atoi(v.c_str());
    else if (k == "ignore") g_cfg.ignore = v;
    else if (k == "backup_dir") g_cfg.backupDir = v;
    else if (k == "review_share") g_cfg.reviewShare = std::clamp(atof(v.c_str()), 0.0, 1.0);
    else if (k == "group_works") g_cfg.groupWorks = atoi(v.c_str()) != 0;
    else if (k == "swipe_buttons") g_cfg.swipeButtons = atoi(v.c_str()) != 0;
    else if (k == "key_mode") g_cfg.keyMode = std::clamp(atoi(v.c_str()), 1, 4);
    else if (k == "throw_delay_ms") g_cfg.throwDelayMs = std::clamp(atoi(v.c_str()), 0, 1000), sawThrowDelay = true;
    else if (k == "long_press_ms") g_cfg.longPressMs = std::clamp(atoi(v.c_str()), 150, 1000);
    else if (k == "advanced_mode") g_cfg.advanced = atoi(v.c_str()) != 0;
    else if (k == "throw_mode") g_cfg.throwSlow = v == "slow", sawThrowMode = true;
    else if (k == "over_points") g_cfg.overPoints = atoi(v.c_str()) != 0;
    else if (k == "remote_access") g_cfg.remote = atoi(v.c_str()) != 0;
    else if (k == "custom_keys") g_cfg.customKeys = validBindings(v) ? v : "";
    else if (k == "custom_mouse") g_cfg.customMouse = validBindings(v) ? v : "";
    else g_unknownConfig.push_back(line);
  }
  // Settings from v0.4: a redirect time of more than 0 becomes Slow.
  if (!sawThrowMode && sawThrowDelay) g_cfg.throwSlow = g_cfg.throwDelayMs > 0;
  auto trim = [](std::string& r) {
    while (r.size() > 3 && (r.back() == '\\' || r.back() == '/')) r.pop_back();
  };
  trim(g_cfg.root);
  g_cfg.moreRoots.clear();
  g_cfg.maxDepths.assign(1, depths.count(1) ? depths[1] : -1);
  for (auto& [n, v] : more) {
    std::string r = v;
    trim(r);
    if (r.empty()) continue;
    g_cfg.moreRoots.push_back(r);
    g_cfg.maxDepths.push_back(depths.count(n) ? depths[n] : -1);
  }
}

void saveConfig() {
  std::string roots;
  for (size_t i = 0; i < g_cfg.moreRoots.size(); ++i)
    roots += "\nroot" + std::to_string(i + 2) + "=" + g_cfg.moreRoots[i];
  for (size_t i = 0; i <= g_cfg.moreRoots.size(); ++i)  // only folders with a limit
    if (maxDepthOf((int)i) >= 0)
      roots += "\nroot" + (i ? std::to_string(i + 1) : std::string()) + "_max_depth=" + std::to_string(maxDepthOf((int)i));
  std::string s = "# Choosen settings\nroot=" + g_cfg.root + roots + "\nartist_depth=" + std::to_string(g_cfg.artistDepth) +
                  "\nmouse_mode=" + std::to_string(g_cfg.mouseMode) + "\nport=" + std::to_string(g_cfg.port) +
                  "\nignore=" + g_cfg.ignore + "\nbackup_dir=" + g_cfg.backupDir +
                  "\nreview_share=" + fmtNum(g_cfg.reviewShare, 3) +
                  "\ngroup_works=" + (g_cfg.groupWorks ? "1" : "0") + "\nkey_mode=" + std::to_string(g_cfg.keyMode) +
                  "\nthrow_delay_ms=" + std::to_string(g_cfg.throwDelayMs) +
                  "\nlong_press_ms=" + std::to_string(g_cfg.longPressMs) +
                  "\nadvanced_mode=" + (g_cfg.advanced ? "1" : "0") +
                  "\nthrow_mode=" + (g_cfg.throwSlow ? "slow" : "fast") +
                  "\nover_points=" + (g_cfg.overPoints ? "1" : "0") + "\nswipe_buttons=" + (g_cfg.swipeButtons ? "1" : "0") +
                  "\nremote_access=" + (g_cfg.remote ? "1" : "0") +
                  "\ncustom_keys=" + g_cfg.customKeys + "\ncustom_mouse=" + g_cfg.customMouse + "\n";
  for (const auto& line : g_unknownConfig) s += line + "\n";
  writeFileAtomic(g_dataDir + L"\\config.txt", s);
}

static void coreInitLocked();

void coreInit() {
  coreInitLocked();
  backupJournal();
}

void coreDailyBackup() {
  const int last = lastBackupDay.load();  // 0 = coreInit has not made the first copy yet
  if (last != 0 && last != todayNumber()) backupJournal();
}

static void coreInitLocked() {
  std::lock_guard<std::mutex> lk(mu);
  int64_t t0 = nowMs();
  sessionSeed = mix64((uint64_t)t0 ^ ((uint64_t)platProcessId() << 32));
  rootName = rootNameOf(g_cfg.root);
  dropTornTail();
  loadIndex();
  loadJournal();
  for (auto& it : items)  // journal order can differ from tick order (e.g. two old instances wrote it)
    std::stable_sort(it.ev.begin(), it.ev.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
  sessionTick0 = tick;
  g_policy.reviewShareMin = g_policy.reviewShareMax = g_cfg.reviewShare;
  for (auto& it : items) computeItem(it);
  rebuildFolders();
  logLine("init: " + std::to_string(items.size()) + " items, tick " + std::to_string(tick) + ", " +
          std::to_string(nowMs() - t0) + " ms");
}

void coreStartScan() {
  if (g_cfg.root.empty()) return;
  int gen = ++scanGen;
  std::thread(scanThread, gen).detach();
}

void coreSaveIndex() {
  std::string s1, s2;
  {
    std::lock_guard<std::mutex> lk(mu);
    const std::string stamp = std::to_string(nowMs());
    const auto roots = photoRoots();
    s1.reserve(items.size() * 96);
    s2.reserve(items.size() * 100);
    s1 += "#choosen-index-1\t" + g_cfg.root + "\t" + stamp + "\n";
    s2 += "#choosen-index-2\t" + stamp + "\n";
    for (int r = 0; r < (int)roots.size(); ++r) s2 += "#root\t" + std::to_string(r) + "\t" + roots[r] + "\n";
    for (auto& it : items) {
      if (!it.present) continue;
      std::string head = hex64(it.fp) + "\t" + std::to_string(it.size) + "\t" + std::to_string(it.mtime) + "\t";
      s2 += head + std::to_string(it.root) + "\t" + it.rel + "\n";
      if (it.root == 0) s1 += head + it.rel + "\n";
    }
    for (auto& c : copies)  // v2 only, after the images: older exes skip a known fingerprint
      s2 += hex64(c.fp) + "\t" + std::to_string(c.size) + "\t" + std::to_string(c.mtime) + "\t" +
            std::to_string(c.root) + "\t" + c.rel + "\tcopy\n";
  }
  writeFileAtomic(index2Path(), s2);
  writeFileAtomic(indexPath(), s1);
}

uint64_t coreVersion() { return version.load(); }

std::string coreSetConfig(const ConfigPatch& p) {
  auto clean = [](std::string s) {
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
  };
  // Validate everything first: a failed save must not leave half of it applied.
  std::string root = p.root ? clean(*p.root) : std::string(), backup = p.backupDir ? clean(*p.backupDir) : std::string();
  if (p.root && !root.empty() && !isDirectory(widen(root))) return "フォルダが見つかりません: " + root;
  // The photo folders after this save; empty entries are dropped, and if the first one is left
  // empty the next one becomes the first.
  // Each folder keeps its subfolder limit: the one sent with it, else the one it has now.
  auto currentDepth = [](const std::string& path) {
    const auto roots = photoRoots();
    for (int r = 0; r < (int)roots.size(); ++r)
      if (toLower(roots[r]) == toLower(path)) return maxDepthOf(r);
    return -1;
  };
  std::vector<std::string> newRoots;
  std::vector<int> newDepths;
  {
    std::vector<std::string> sent;
    sent.push_back(p.root ? root : g_cfg.root);
    for (const auto& m : p.moreRoots ? *p.moreRoots : g_cfg.moreRoots) sent.push_back(clean(m));
    for (size_t i = 0; i < sent.size(); ++i) {
      if (sent[i].empty()) continue;
      const int d = p.maxDepths && i < p.maxDepths->size() ? (*p.maxDepths)[i] : currentDepth(sent[i]);
      if (d < -1 || d > kMaxSubfolderDepth)
        return "サブフォルダの階層は 0〜" + std::to_string(kMaxSubfolderDepth) + " で指定してください";
      newRoots.push_back(sent[i]);
      newDepths.push_back(d);
    }
  }
  if (p.moreRoots)
    for (const auto& m : newRoots)
      if (!isDirectory(widen(m))) return "フォルダが見つかりません: " + m;
  // Photo folders must not overlap, or an image would be counted in two of them. Compared as full
  // paths (".." resolved), ignoring letter case and a trailing backslash. Junctions and other
  // links to the same folder are not detected.
  if (p.root || p.moreRoots) {
    auto key = [](const std::string& path) {
      std::string s = toLower(platFullPath(path));
      std::replace(s.begin(), s.end(), '/', '\\');  // one separator, whatever the platform returns
      if (s.empty() || s.back() != '\\') s += '\\';
      return s;
    };
    for (size_t i = 0; i < newRoots.size(); ++i)
      for (size_t j = i + 1; j < newRoots.size(); ++j) {
        const std::string a = key(newRoots[i]), b = key(newRoots[j]);
        if (a == b) return "同じフォルダが 2 回入っています: " + newRoots[j];
        if (b.compare(0, a.size(), a) == 0)
          return "「" + newRoots[j] + "」は写真フォルダ「" + newRoots[i] + "」の中にあります。重なるフォルダは追加できません";
        if (a.compare(0, b.size(), b) == 0)
          return "「" + newRoots[j] + "」の中に写真フォルダ「" + newRoots[i] + "」があります。重なるフォルダは追加できません";
      }
  }
  if (p.backupDir && !backup.empty() && !isDirectory(widen(backup)) && !platMakeDir(widen(backup)))
    return "バックアップ先のフォルダを作れません: " + backup;
  if ((p.customKeys && !validBindings(*p.customKeys)) || (p.customMouse && !validBindings(*p.customMouse)))
    return "操作の割り当てを保存できません";

  bool rescan = false, backupNow = false;
  {
    std::lock_guard<std::mutex> lk(mu);
    bool regroup = false;
    if (p.backupDir) {
      backupNow = !backup.empty() && toLower(backup) != toLower(g_cfg.backupDir);
      g_cfg.backupDir = backup;
    }
    const std::vector<std::string> oldRoots = photoRoots();
    auto same = [](const std::vector<std::string>& a, const std::vector<std::string>& b) {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i)
        if (toLower(a[i]) != toLower(b[i])) return false;
      return true;
    };
    if (!same(oldRoots, newRoots)) {
      g_cfg.root = newRoots.empty() ? std::string() : newRoots[0];
      g_cfg.moreRoots.assign(newRoots.size() > 1 ? newRoots.begin() + 1 : newRoots.end(), newRoots.end());
      copies.clear();  // found and checked again by the rescan
      rootName = rootNameOf(g_cfg.root);
      // Images of a folder that stays keep their place under its new position. Images of a folder
      // that was removed leave the library; their ratings stay in the journal, and when their files
      // are found again (in any photo folder) they are matched by fingerprint and come back rated.
      for (auto& it : items) {
        int to = -1;
        if (it.root >= 0 && it.root < (int)oldRoots.size())
          for (int j = 0; j < (int)newRoots.size(); ++j)
            if (toLower(newRoots[j]) == toLower(oldRoots[it.root])) to = j;
        if (to >= 0) {
          it.root = to;
          continue;
        }
        const bool absolute = it.rel.size() > 1 && (it.rel[1] == ':' || it.rel.compare(0, 2, "\\\\") == 0);
        if (it.root >= 0 && it.root < (int)oldRoots.size() && !it.rel.empty() && !absolute)
          it.rel = oldRoots[it.root] + "\\" + slashToBack(it.rel);  // like a journal entry of an unknown folder
        it.root = 0;
        it.present = false;
        it.size = -1;
      }
      regroup = true;
      rescan = !newRoots.empty();
    }
    // A changed subfolder limit changes which images are read (a rescan hides or brings them
    // back; their ratings stay) and which folders count as artists.
    std::vector<int> oldDepths;
    for (int r = 0; r < (int)newRoots.size(); ++r) oldDepths.push_back(maxDepthOf(r));
    g_cfg.maxDepths = newDepths;
    if (oldDepths != newDepths) {
      regroup = true;
      rescan = !newRoots.empty();
    }
    if (p.groupWorks == 0 || p.groupWorks == 1) {
      regroup |= (p.groupWorks == 1) != g_cfg.groupWorks;
      g_cfg.groupWorks = p.groupWorks == 1;
    }
    if (regroup) rebuildFolders();  // regroups the cards; ratings are per file and stay as they are
    if (p.artistDepth >= 1 && std::min(p.artistDepth, 16) != g_cfg.artistDepth) {
      g_cfg.artistDepth = std::min(p.artistDepth, 16);
      computeFolders();  // which folders are artists depends on it
      baselineDirty = true;
    }
    if (p.mouseMode >= 1 && p.mouseMode <= 4) g_cfg.mouseMode = p.mouseMode;
    if (p.keyMode >= 1 && p.keyMode <= 4) g_cfg.keyMode = p.keyMode;
    if (p.advanced == 0 || p.advanced == 1) g_cfg.advanced = p.advanced == 1;
    if (p.throwSlow == 0 || p.throwSlow == 1) g_cfg.throwSlow = p.throwSlow == 1;
    if (p.overPoints == 0 || p.overPoints == 1) g_cfg.overPoints = p.overPoints == 1;
    if (p.swipeButtons == 0 || p.swipeButtons == 1) g_cfg.swipeButtons = p.swipeButtons == 1;
    if (p.remote == 0 || p.remote == 1) g_cfg.remote = p.remote == 1;
    if (p.customKeys) g_cfg.customKeys = *p.customKeys;
    if (p.customMouse) g_cfg.customMouse = *p.customMouse;
    if (p.reviewShare >= 0) {
      g_cfg.reviewShare = std::clamp(p.reviewShare, 0.0, 1.0);
      g_policy.reviewShareMin = g_policy.reviewShareMax = g_cfg.reviewShare;
    }
    if (p.throwDelayMs >= 0) g_cfg.throwDelayMs = std::clamp(p.throwDelayMs, 0, 1000);
    if (p.longPressMs >= 0) g_cfg.longPressMs = std::clamp(p.longPressMs, 150, 1000);
    version++;
  }
  saveConfig();
  if (backupNow) backupJournal();
  if (rescan) coreStartScan();
  return "";
}

int coreHiddenBy(const std::string& rootPath, int maxDepth) {
  std::lock_guard<std::mutex> lk(mu);
  const auto roots = photoRoots();
  int root = -1;
  for (int r = 0; r < (int)roots.size(); ++r)
    if (toLower(roots[r]) == toLower(rootPath)) root = r;
  if (root < 0 || maxDepth < 0) return 0;
  int n = 0;
  for (const Item& it : items)
    if (it.present && it.root == root && (int)std::count(it.rel.begin(), it.rel.end(), '/') > maxDepth) n++;
  return n;
}

bool itemFile(int id, const std::string& fpHex, std::wstring& path, uint64_t& fp, std::string& ext) {
  std::lock_guard<std::mutex> lk(mu);
  int idx = resolveItem(id, fpHex);
  if (idx < 0 || !items[idx].present) return false;
  path = absPath(items[idx]);
  fp = items[idx].fp;
  ext = extOf(items[idx].rel);
  return true;
}

void coreResetForTest() {
  std::lock_guard<std::mutex> lk(mu);
  scanGen++;  // cancels any running scan
  items.clear();
  copies.clear();
  works.clear();
  byFp.clear();
  folders.clear();
  folderByPath.clear();
  sessionTick0 = 0;
  baselineDirty = true;
  cidMap.clear();
  replacedBy.clear();
  backupWarning.clear();
  scanError.clear();
  tick = 0;
  sessionSwipes = 0;
  version++;
}

void coreScanSync() {
  int gen = ++scanGen;
  scanThread(gen);
}

int coreDebugHashed() { return scanToHash.load(); }

int coreDebugCopies() {
  std::lock_guard<std::mutex> lk(mu);
  return (int)copies.size();
}

bool coreDebugItem(const std::string& rel, DebugItem& out) {
  std::lock_guard<std::mutex> lk(mu);
  for (int i = 0; i < (int)items.size(); ++i) {
    const Item& it = items[i];
    if (it.rel != rel) continue;
    out.id = i;
    out.rel = it.rel;
    out.root = it.root;
    out.fp = hex64(it.fp);
    out.present = it.present;
    out.rated = it.rated;
    out.archived = it.archived;
    out.p = it.p;
    out.S = it.S;
    out.dueIn = it.touched ? (int64_t)it.due - (int64_t)tick : 0;
    out.n = it.nRatings;
    out.nL = it.nLike;
    out.nS = it.nSuper;
    out.events = (int)it.ev.size();
    out.last = it.last;
    return true;
  }
  return false;
}

bool coreDebugFolder(const std::string& path, DebugFolder& out) {
  std::lock_guard<std::mutex> lk(mu);
  auto f = folderByPath.find(path);
  if (f == folderByPath.end()) return false;
  const Folder& F = folders[f->second];
  out.path = F.path;
  out.depth = F.depth;
  out.total = F.total;
  out.images = F.images;
  out.rated = F.rated;
  out.liked = F.liked;
  out.rank = F.rank;
  out.score = F.score;
  out.artist = isArtist(F);
  return true;
}

uint64_t coreDebugTick() {
  std::lock_guard<std::mutex> lk(mu);
  return tick;
}

DebugItem coreDebugSchedule(const std::string& actions, const std::vector<uint64_t>& ticks) {
  Item it;
  it.fp = 0x1234567890abcdefULL;
  for (size_t i = 0; i < actions.size() && i < ticks.size(); ++i) it.ev.push_back({ticks[i], 0, actions[i]});
  computeItem(it);
  DebugItem d;
  d.S = it.S;
  d.p = it.p;
  d.rated = it.rated;
  d.n = it.nRatings;
  d.nL = it.nLike;
  d.nS = it.nSuper;
  d.last = it.last;
  d.events = (int)it.ev.size();
  d.dueIn = it.ev.empty() ? 0 : (int64_t)it.due - (int64_t)it.ev.back().tick;
  return d;
}

void coreSetPolicy(double shareMin, double shareMax, double backlogHalf, double passWeight, double lateCap) {
  std::lock_guard<std::mutex> lk(mu);
  g_policy.reviewShareMin = shareMin;
  g_policy.reviewShareMax = shareMax;
  g_policy.backlogHalf = backlogHalf;
  g_policy.passWeight = passWeight;
  g_policy.lateCap = lateCap;
  for (auto& it : items) computeItem(it);
  computeFolders();
}

void coreDebugJournalHook(void (*f)()) { g_journalHook = f; }
