// The rating journal (append-only log of every rating) and its backups.
#include "core_internal.h"

namespace core {

std::string localDay(int64_t ms) {
  int y, m, d;
  platLocalDate(ms, y, m, d);
  char b[16];
  snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
  return b;
}

// Journal line: tick \t unix_ms \t fingerprint \t action \t absolute path [\t key=value ...]
// Keys: cid=<client rating id> on ratings; target=<tick> on X (revoke that exact rating).
// Lines written by v0.1 have no keys; their X revokes the item's latest event.
std::string journalField(const std::vector<std::string>& f, const char* key) {
  size_t kl = strlen(key);
  for (size_t i = 5; i < f.size(); ++i)
    if (f[i].size() > kl && f[i].compare(0, kl, key) == 0 && f[i][kl] == '=') return f[i].substr(kl + 1);
  return "";
}

void loadJournal() {
  std::string s;
  if (!readFileBytes(journalPath(), s)) return;
  // a correction's lines: its X lines, then its new ratings, with one tick and one time; the
  // replaced rating of each picture leads to the new one there (replacedBy)
  uint64_t xTick = UINT64_MAX;
  int64_t xMs = 0;
  std::vector<std::pair<uint64_t, uint64_t>> xTargets;  // (fp, target tick) of the latest X lines
  size_t a = 0;
  while (a < s.size()) {
    size_t b = s.find('\n', a);
    if (b == std::string::npos) b = s.size();
    std::string line = s.substr(a, b - a);
    a = b + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto f = split(line, '\t');
    if (f.size() < 4 || f[3].size() != 1) continue;
    uint64_t t = std::strtoull(f[0].c_str(), nullptr, 10);
    int64_t ms = std::strtoll(f[1].c_str(), nullptr, 10);
    uint64_t fp = parseHex64(f[2]);
    char act = f[3][0];
    if (!fp || f[2].size() != 16) continue;
    int idx;
    auto it = byFp.find(fp);
    if (it != byFp.end()) {
      idx = it->second;
    } else {
      Item ph;  // rated file not currently indexed (moved, renamed or deleted)
      ph.fp = fp;
      if (f.size() > 4) ph.rel = relFromAbs(f[4], ph.root);
      ph.size = -1;
      idx = (int)items.size();
      byFp[fp] = idx;
      items.push_back(std::move(ph));
    }
    Item& item = items[idx];
    if (t != xTick || ms != xMs) xTargets.clear();  // not the same request as the X lines before
    if (act == 'X') {
      // a cancel's tick is used up too: a rating after a restart must never share it
      tick = std::max(tick, t + 1);
      if (t != xTick || ms != xMs) xTick = t, xMs = ms;
      std::string target = journalField(f, "target");
      if (target.empty()) {
        if (!item.ev.empty()) item.ev.pop_back();
      } else {
        uint64_t tt = std::strtoull(target.c_str(), nullptr, 10);
        if (t > tt) xTargets.push_back({fp, tt});
        for (size_t k = item.ev.size(); k-- > 0;)
          if (item.ev[k].tick == tt) {
            item.ev.erase(item.ev.begin() + (ptrdiff_t)k);
            break;
          }
      }
      continue;
    }
    // L like, S super, D pass, K hold, A archive (never show again), R restore from the archive
    if (act != 'L' && act != 'S' && act != 'D' && act != 'K' && act != 'A' && act != 'R') continue;
    for (const auto& [xfp, xt] : xTargets)
      if (xfp == fp) replacedBy[{fp, xt}] = t;  // this rating is the correction of that one
    std::string cid = journalField(f, "cid");
    if (!cid.empty()) {
      auto& refs = cidMap[cid];
      bool dup = false;  // the same rating written twice (a retry after a failed flush): keep one
      for (auto& r : refs) dup |= r.fp == fp;
      if (dup) continue;
      refs.push_back({fp, t});
    }
    item.ev.push_back({t, ms, act});
    tick = std::max(tick, t + 1);
  }
}

// A crash in the middle of a write can leave a torn last line. That rating was never
// acknowledged (the page still has it and will send it again), so the fragment is cut off
// instead of being replayed.
void dropTornTail() {
  PlatFile* h = platOpen(journalPath(), PlatOpen::ReadWrite);
  if (!h) return;
  const int64_t size = platSize(h);
  int64_t end = size;
  char buf[4096];
  while (end > 0) {  // scan back to the last newline
    const size_t want = (size_t)std::min<int64_t>(sizeof buf, end);
    const int64_t off = end - (int64_t)want;
    if (platReadAt(h, off, buf, want) != want) break;
    int k = (int)want - 1;
    while (k >= 0 && buf[k] != '\n') k--;
    if (k >= 0) {
      end = off + k + 1;
      break;
    }
    end = off;
  }
  if (end < size) {
    platTruncate(h, end);
    platFlush(h);
    logLine("journal: dropped a torn last line (" + std::to_string(size - end) + " bytes)");
  }
  platClose(h);
}

// Appends and forces the bytes to disk before the rating is acknowledged. On any failure the
// file is cut back to where it was, so an unacknowledged rating never survives in the journal.
bool appendJournal(const std::string& lines) {
  PlatFile* h = platOpen(journalPath(), PlatOpen::Append);
  if (!h) return false;
  const int64_t start = platSize(h);
  bool ok = start >= 0 && platWriteAt(h, start, lines);
  if (ok && g_journalFlush) ok = platFlush(h);
  if (!ok && start >= 0) platTruncate(h, start);
  platClose(h);
  return ok;
}

int64_t fileSize(const std::wstring& p) { return platFileSize(p); }

std::vector<std::wstring> listBackups(const std::wstring& dir) {
  std::vector<std::wstring> all;
  std::vector<DirEntry> entries;
  platListDir(dir, entries);
  for (auto& e : entries)
    if (!e.dir && e.name.size() > 12 && e.name.compare(0, 8, L"ratings-") == 0 &&
        e.name.compare(e.name.size() - 4, 4, L".log") == 0)
      all.push_back(e.name);
  std::sort(all.begin(), all.end());  // names are ratings-YYYYMMDD.log
  return all;
}

int todayNumber() {
  int y, m, d;
  platLocalDate(nowMs(), y, m, d);
  return y * 10000 + m * 100 + d;
}

// Copy of the journal, one file per day, plus the settings. The journal only ever grows, so a
// journal smaller than the newest backup means something went wrong: then keep every backup and
// warn instead of rotating good copies out. Returns a warning for the page ("" = fine).
std::string backupTo(const std::wstring& dir, size_t keep) {
  int64_t cur = fileSize(journalPath());
  if (cur <= 0) return "";
  if (!platMakeDir(dir)) {
    logLine("backup: cannot create " + narrow(dir));
    return "バックアップ先に書き込めません（ドライブが外れていませんか）: " + narrow(dir);
  }
  auto all = listBackups(dir);
  if (!all.empty() && fileSize(dir + L"\\" + all.back()) > cur) {
    logLine("WARNING: ratings.log is smaller than the newest backup in " + narrow(dir) + "; backups kept");
    return "評価ファイル (ratings.log) が最新のバックアップより小さくなっています。バックアップは削除せずに残しています: " + narrow(dir);
  }
  wchar_t name[64];
  swprintf(name, 64, L"\\ratings-%08d.log", todayNumber());
  if (!platCopyFile(journalPath(), dir + name)) {
    logLine("backup: copy failed to " + narrow(dir));
    return "バックアップをコピーできませんでした: " + narrow(dir);
  }
  platCopyFile(g_dataDir + L"\\config.txt", dir + L"\\config.txt");
  all = listBackups(dir);
  for (size_t i = 0; i + keep < all.size(); ++i) platDeleteFile(dir + L"\\" + all[i]);
  return "";
}

// Holds the journal lock while copying, so a rating written at that moment cannot leave a
// half-copied last line. Never called with `mu` held (the rating path takes mu, then jmu).
void backupJournal() {
  std::string warn, extra;
  {
    std::lock_guard<std::mutex> lk(mu);
    extra = g_cfg.backupDir;
  }
  {
    std::lock_guard<std::mutex> lk(jmu);
    warn = backupTo(g_dataDir + L"\\backup", 14);
    if (!extra.empty()) {
      std::string w = backupTo(widen(extra), 60);
      if (!w.empty()) warn = warn.empty() ? w : warn + " / " + w;
    }
  }
  lastBackupDay = todayNumber();
  std::lock_guard<std::mutex> lk(mu);
  if (warn != backupWarning) {
    backupWarning = warn;
    version++;
  }
}

}  // namespace core

