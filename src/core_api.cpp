// JSON answers for the page: state, folders, statistics, card lists and rating.
#include "core_internal.h"

namespace core {

// JSON for one card. `i` is the image shown; its rating state comes from `stateOf` (the work's
// lead page); `pagesOf` >= 0 adds the work's page list for flipping through it.
std::string itemJson(int i, const char* kind, int stateOf, const std::vector<int>* pages) {
  const Item& shown = items[i];
  const Item& it = items[stateOf >= 0 ? stateOf : i];
  std::string name = baseName(shown.rel);
  std::string dir = folderKeyOf(shown);
  std::string o;
  o.reserve(256);
  o += "{\"id\":" + std::to_string(i);
  o += ",\"fp\":\"" + hex64(shown.fp) + "\"";
  o += ",\"name\":" + jsonStr(name);
  o += ",\"dir\":" + jsonStr(dir);
  o += ",\"ext\":" + jsonStr(extOf(shown.rel));
  if (pages && pages->size() > 1) {
    o += ",\"pages\":[";
    for (size_t k = 0; k < pages->size(); ++k) {
      const int pi = (*pages)[k];
      const Item& pg = items[pi];
      if (k) o += ',';
      o += "{\"id\":" + std::to_string(pi) + ",\"fp\":\"" + hex64(pg.fp) + "\",\"ext\":" + jsonStr(extOf(pg.rel)) +
           ",\"name\":" + jsonStr(baseName(pg.rel)) + "}";
    }
    o += "]";
  }
  if (it.archived) o += ",\"archived\":true";
  o += std::string(",\"rated\":") + (it.rated ? "true" : "false");
  o += ",\"p\":" + fmtNum(it.p, 0);
  o += ",\"n\":" + std::to_string(it.nRatings);
  if (it.nLike) o += ",\"nL\":" + std::to_string(it.nLike);
  if (it.nSuper) o += ",\"nS\":" + std::to_string(it.nSuper);
  o += ",\"last\":\"" + (it.last ? std::string(1, it.last) : std::string()) + "\"";
  o += ",\"dueIn\":" + std::to_string(it.touched ? (int64_t)it.due - (int64_t)tick : 0);
  o += ",\"ms\":" + std::to_string(it.lastMs);
  if (it.hold) o += ",\"hold\":true";
  if (kind && *kind) o += std::string(",\"kind\":\"") + kind + "\"";
  o += "}";
  return o;
}

// Ids are positions in `items`, which can differ between runs; the fingerprint is the real identity.
int resolveItem(int id, const std::string& fpHex) {
  uint64_t fp = fpHex.size() == 16 ? parseHex64(fpHex) : 0;
  if (id >= 0 && id < (int)items.size() && (!fp || items[id].fp == fp)) return id;
  if (fp) {
    auto f = byFp.find(fp);
    if (f != byFp.end()) return f->second;
  }
  return -1;
}

bool validCid(const std::string& c) {
  if (c.size() > 48) return false;
  for (char ch : c)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_'))
      return false;
  return true;
}

}  // namespace core

using namespace core;

std::string apiState() {
  std::lock_guard<std::mutex> lk(mu);
  int present = 0, rated = 0, due = 0, fresh = 0, archived = 0;  // rated/due/fresh: per card
  for (auto& it : items)
    if (it.present) {
      if (it.archived) archived++;
      else present++;
    }
  for (const Work& w : works) {
    const Item& it = items[leadOf(w)];
    if (it.rated) rated++;
    if (!it.touched) fresh++;
    else if (it.due <= tick) due++;
  }
  std::string o = "{\"app\":\"choosen\",\"version\":" + std::to_string(version.load());
  o += ",\"instance\":\"" + hex64(sessionSeed) + "\"";
  o += ",\"warning\":" + jsonStr(backupWarning);
  o += ",\"backupDir\":" + jsonStr(g_cfg.backupDir);
  o += ",\"reviewShare\":" + fmtNum(g_cfg.reviewShare, 3);
  o += std::string(",\"groupWorks\":") + (g_cfg.groupWorks ? "true" : "false");
  o += std::string(",\"swipeButtons\":") + (g_cfg.swipeButtons ? "true" : "false");
  o += ",\"works\":" + std::to_string(works.size());
  o += ",\"groupedCount\":" + std::to_string(groupedCount);
  o += ",\"root\":" + jsonStr(g_cfg.root);
  o += ",\"moreRoots\":[";
  for (size_t i = 0; i < g_cfg.moreRoots.size(); ++i) o += (i ? "," : "") + jsonStr(g_cfg.moreRoots[i]);
  o += "],\"maxDepths\":[";  // one per photo folder: the first, then moreRoots
  for (size_t i = 0; i <= g_cfg.moreRoots.size(); ++i) o += (i ? "," : "") + std::to_string(maxDepthOf((int)i));
  o += "],\"maxSubfolderDepth\":" + std::to_string(kMaxSubfolderDepth);
  o += std::string(",\"hasRoot\":") + (g_cfg.root.empty() ? "false" : "true");
  o += std::string(",\"scanning\":") + (scanning ? "true" : "false");
  o += ",\"scanError\":" + jsonStr(scanError);
  o += ",\"found\":" + std::to_string(scanFound.load());
  o += ",\"hashed\":" + std::to_string(scanHashed.load());
  o += ",\"toHash\":" + std::to_string(scanToHash.load());
  o += ",\"dupes\":" + std::to_string(scanDupes.load());
  o += ",\"copies\":" + std::to_string(copies.size());
  o += std::string(",\"remoteAccess\":") + (g_cfg.remote ? "true" : "false");
  o += ",\"items\":" + std::to_string(present);
  o += ",\"rated\":" + std::to_string(rated);
  o += ",\"due\":" + std::to_string(due);
  o += ",\"fresh\":" + std::to_string(fresh);
  o += ",\"tick\":" + std::to_string(tick);
  o += ",\"session\":" + std::to_string(sessionSwipes);
  o += ",\"artistDepth\":" + std::to_string(g_cfg.artistDepth);
  o += ",\"mouseMode\":" + std::to_string(g_cfg.mouseMode);
  o += ",\"keyMode\":" + std::to_string(g_cfg.keyMode);
  o += ",\"throwDelayMs\":" + std::to_string(g_cfg.throwDelayMs);
  o += ",\"longPressMs\":" + std::to_string(g_cfg.longPressMs);
  o += ",\"throwMs\":" + std::to_string(effectiveThrowMs());
  o += ",\"holdMs\":" + std::to_string(effectiveHoldMs());
  o += std::string(",\"advanced\":") + (g_cfg.advanced ? "true" : "false");
  o += std::string(",\"throwSlow\":") + (g_cfg.throwSlow ? "true" : "false");
  o += std::string(",\"overPoints\":") + (g_cfg.overPoints ? "true" : "false");
  o += ",\"customKeys\":" + jsonStr(g_cfg.customKeys);
  o += ",\"customMouse\":" + jsonStr(g_cfg.customMouse);
  o += ",\"archived\":" + std::to_string(archived);
  o += ",\"ignore\":" + jsonStr(g_cfg.ignore);
  o += "}";
  return o;
}

std::string apiFolders(int64_t sinceMs) {
  std::lock_guard<std::mutex> lk(mu);
  if (baselineDirty) computeBaseline();
  // rank0/score0 = the comparison point: this session's start, or a moment in the past
  std::vector<int> rank0, rated0;
  std::vector<double> score0;
  if (sinceMs > 0) {
    ranksBefore(true, (uint64_t)sinceMs, rank0, score0, rated0);
  } else {
    ranksBefore(false, sessionTick0, rank0, score0, rated0);
  }
  std::string o;
  o.reserve(folders.size() * 180);
  o += "{\"artistDepth\":" + std::to_string(g_cfg.artistDepth) + ",\"since\":" + std::to_string(sinceMs) +
       ",\"folders\":[";
  for (size_t i = 0; i < folders.size(); ++i) {
    const Folder& f = folders[i];
    if (i) o += ',';
    o += "{\"path\":" + jsonStr(f.path) + ",\"name\":" + jsonStr(f.name) + ",\"depth\":" + std::to_string(f.depth) +
         ",\"total\":" + std::to_string(f.total) + ",\"images\":" + std::to_string(f.images) +
         ",\"rated\":" + std::to_string(f.rated) +
         ",\"liked\":" + std::to_string(f.liked) + ",\"supers\":" + std::to_string(f.supers) +
         ",\"score\":" + fmtNum(f.score, 1) + ",\"rank\":" + std::to_string(f.rank) +
         (isArtist(f) ? ",\"artist\":true" : "") + ",\"rank0\":" + std::to_string(rank0[i]) + ",\"score0\":" + fmtNum(score0[i], 1) +
         ",\"rated0\":" + std::to_string(rated0[i]) + "}";
  }
  o += "]}";
  return o;
}

// Swipes per local day for the last `days` days (today last), for the statistics panel.
std::string apiStats(int days) {
  std::lock_guard<std::mutex> lk(mu);
  days = std::clamp(days, 1, 366);
  struct Day {
    int like = 0, super = 0, pass = 0, hold = 0, archive = 0;
  };
  std::unordered_map<std::string, Day> byDay;
  int64_t from = nowMs() - (int64_t)days * 86400000LL - 86400000LL;
  int totalRated = 0, liked = 0, supers = 0;
  for (const Work& w : works) {
    const Item& it = items[leadOf(w)];
    if (it.rated) {
      totalRated++;
      liked += it.last != 'D';
      supers += it.last == 'S';
    }
  }
  std::unordered_set<uint64_t> seen;  // one swipe = one tick, even when it rated every page of a work
  for (auto& it : items) {
    for (auto& e : it.ev) {
      if (e.ms < from || !seen.insert(e.tick).second) continue;
      Day& d = byDay[localDay(e.ms)];
      if (e.a == 'L') d.like++;
      else if (e.a == 'S') d.super++;
      else if (e.a == 'D') d.pass++;
      else if (e.a == 'K') d.hold++;
      else if (e.a == 'A') d.archive++;
    }
  }
  std::string o = "{\"rated\":" + std::to_string(totalRated) + ",\"liked\":" + std::to_string(liked) +
                  ",\"supers\":" + std::to_string(supers) + ",\"days\":[";
  int64_t now = nowMs();
  for (int k = days - 1; k >= 0; --k) {
    std::string key = localDay(now - (int64_t)k * 86400000LL);
    Day d;
    auto f = byDay.find(key);
    if (f != byDay.end()) d = f->second;
    if (k != days - 1) o += ',';
    o += "{\"day\":\"" + key + "\",\"like\":" + std::to_string(d.like) + ",\"super\":" + std::to_string(d.super) +
         ",\"pass\":" + std::to_string(d.pass) + ",\"hold\":" + std::to_string(d.hold) +
         ",\"archive\":" + std::to_string(d.archive) + "}";
  }
  o += "]}";
  return o;
}

std::string apiItems(const std::string& filter, const std::string& scope, const std::string& sort, int offset,
                     int limit, uint64_t seed) {
  std::lock_guard<std::mutex> lk(mu);
  auto m = scopeMask(scope);
  struct Card {
    int cover, lead;
    const std::vector<int>* pages;
  };
  std::vector<Card> cards;
  std::vector<std::vector<int>> archived;
  if (filter == "archived") {
    archived = archivedGroups(m);
    for (auto& g : archived) cards.push_back({g[0], g[0], &g});
  } else {
    for (const Work& W : works) {
      if (W.folder < 0 || !m[W.folder]) continue;
      const int l = leadOf(W);
      if (cardMatches(filter, items[l])) cards.push_back({W.pages[0], l, &W.pages});
    }
  }
  auto S = [&](const Card& c) -> const Item& { return items[c.lead]; };
  // Every comparator ends on the cover id so pages of the same query never overlap or skip.
  if (sort == "folder") {
    // position of each artist folder in ranking order (ranked first, then by path), computed once
    std::vector<int> artist = artistFolders();
    std::vector<int> order(folders.size());
    for (size_t f = 0; f < folders.size(); ++f) order[f] = (int)f;
    std::sort(order.begin(), order.end(), [](int a, int b) {
      int ra = folders[a].rank ? folders[a].rank : INT_MAX, rb = folders[b].rank ? folders[b].rank : INT_MAX;
      if (ra != rb) return ra < rb;
      return folders[a].path < folders[b].path;
    });
    std::vector<int> pos(folders.size());
    for (size_t k = 0; k < order.size(); ++k) pos[order[k]] = (int)k;
    std::sort(cards.begin(), cards.end(), [&](const Card& a, const Card& b) {
      int pa = pos[artist[items[a.cover].folder]], pb = pos[artist[items[b.cover].folder]];
      if (pa != pb) return pa < pb;
      if (S(a).p != S(b).p) return S(a).p > S(b).p;
      return a.cover < b.cover;
    });
  } else if (sort == "recent") {
    std::sort(cards.begin(), cards.end(), [&](const Card& a, const Card& b) {
      if (S(a).lastMs != S(b).lastMs) return S(a).lastMs > S(b).lastMs;
      return a.cover < b.cover;
    });
  } else if (sort == "name") {
    std::sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) {
      if (items[a.cover].rel != items[b.cover].rel) return items[a.cover].rel < items[b.cover].rel;
      return a.cover < b.cover;
    });
  } else if (sort == "random") {  // seed lets "shuffle" give a new order each time
    std::vector<std::pair<uint64_t, size_t>> keyed;
    keyed.reserve(cards.size());
    for (size_t k = 0; k < cards.size(); ++k) keyed.push_back({mix64(items[cards[k].cover].fp ^ sessionSeed ^ seed), k});
    std::sort(keyed.begin(), keyed.end());
    std::vector<Card> shuffled;
    shuffled.reserve(cards.size());
    for (auto& kv : keyed) shuffled.push_back(cards[kv.second]);
    cards.swap(shuffled);
  } else {  // score
    std::sort(cards.begin(), cards.end(), [&](const Card& a, const Card& b) {
      const Item &A = S(a), &B = S(b);
      if (A.p != B.p) return A.p > B.p;
      if (A.nRatings != B.nRatings) return A.nRatings > B.nRatings;
      if (A.lastMs != B.lastMs) return A.lastMs > B.lastMs;
      return a.cover < b.cover;
    });
  }
  offset = std::max(0, offset);
  limit = std::clamp(limit, 1, 1000);
  std::string o = "{\"total\":" + std::to_string(cards.size()) + ",\"items\":[";
  for (int k = offset, c = 0; k < (int)cards.size() && c < limit; ++k, ++c) {
    if (c) o += ',';
    o += itemJson(cards[k].cover, "", cards[k].lead, cards[k].pages);
  }
  o += "]}";
  return o;
}

// The gallery's folder view: one tile per artist folder, with the best matching card as cover.
std::string apiFolderCards(const std::string& filter, const std::string& scope, const std::string& sort) {
  std::lock_guard<std::mutex> lk(mu);
  auto m = scopeMask(scope);
  std::vector<int> artist = artistFolders();
  struct Acc {
    int count = 0, cover = -1;
    double bestP = -1;
    int64_t bestMs = 0;
  };
  std::vector<Acc> acc(folders.size());
  for (const Work& W : works) {
    if (W.folder < 0 || !m[W.folder]) continue;
    const Item& it = items[leadOf(W)];
    if (!cardMatches(filter, it)) continue;
    Acc& a = acc[artist[W.folder]];
    a.count++;
    if (a.cover < 0 || it.p > a.bestP || (it.p == a.bestP && it.lastMs > a.bestMs))
      a.cover = W.pages[0], a.bestP = it.p, a.bestMs = it.lastMs;
  }
  std::vector<int> list;
  for (int f = 0; f < (int)folders.size(); ++f)
    if (acc[f].count > 0) list.push_back(f);
  std::sort(list.begin(), list.end(), [&](int a, int b) {
    if (sort == "count" && acc[a].count != acc[b].count) return acc[a].count > acc[b].count;
    if (sort == "name") return folders[a].path < folders[b].path;
    int ra = folders[a].rank ? folders[a].rank : INT_MAX, rb = folders[b].rank ? folders[b].rank : INT_MAX;
    if (ra != rb) return ra < rb;
    if (acc[a].count != acc[b].count) return acc[a].count > acc[b].count;
    return folders[a].path < folders[b].path;
  });
  std::string o = "{\"total\":" + std::to_string(list.size()) + ",\"folders\":[";
  for (size_t k = 0; k < list.size(); ++k) {
    const Folder& f = folders[list[k]];
    const Item& cv = items[acc[list[k]].cover];
    if (k) o += ',';
    o += "{\"path\":" + jsonStr(f.path) + ",\"name\":" + jsonStr(f.name) + ",\"depth\":" + std::to_string(f.depth) +
         ",\"rank\":" + std::to_string(f.rank) + ",\"score\":" + fmtNum(f.score, 1) +
         ",\"liked\":" + std::to_string(f.liked) + ",\"rated\":" + std::to_string(f.rated) +
         ",\"total\":" + std::to_string(f.total) + ",\"images\":" + std::to_string(f.images) +
         ",\"count\":" + std::to_string(acc[list[k]].count) + ",\"cover\":{\"id\":" +
         std::to_string(acc[list[k]].cover) + ",\"fp\":\"" + hex64(cv.fp) + "\"}}";
  }
  o += "]}";
  return o;
}

// Rates one image. The journal line is written and flushed before memory changes, so an
// acknowledged rating is always on disk; the slow flush (data lives on an HDD) runs outside `mu`
// so choosing the next card never waits for it.
std::string apiRate(int id, const std::string& fpHex, char a, const std::string& cid, const std::string& replaceCid,
                    int& status) {
  std::lock_guard<std::mutex> one(rateMu);  // see core_internal.h
  std::unique_lock<std::mutex> lk(mu);
  if ((a != 'L' && a != 'S' && a != 'D' && a != 'K' && a != 'A' && a != 'R') || fpHex.size() != 16 ||
      !validCid(cid) || !validCid(replaceCid)) {
    status = 400;
    return "{\"error\":\"bad request\"}";
  }
  int idx = resolveItem(id, fpHex);
  if (idx < 0) {
    status = 404;
    return "{\"error\":\"unknown image\"}";
  }
  auto cardJson = [](int anyPage) {
    const int w = items[anyPage].work;
    if (w < 0) return itemJson(anyPage, "");
    return itemJson(works[w].pages[0], "", leadOf(works[w]), &works[w].pages);
  };
  if (!cid.empty() && cidMap.count(cid)) {  // a retry of a rating that was already stored
    return "{\"saved\":true,\"duplicate\":true,\"tick\":" + std::to_string(cidMap[cid].front().tick) +
           ",\"item\":" + cardJson(idx) + ",\"chain\":[]}";
  }
  if (!cid.empty() && !pendingCids.insert(cid).second) {  // the same rating is being written right now
    status = 409;
    return "{\"error\":\"in progress\"}";
  }
  struct Unpend {  // runs with `mu` held on every exit path below
    const std::string& c;
    ~Unpend() {
      if (!c.empty()) pendingCids.erase(c);
    }
  } unpend{cid};
  // One swipe rates the whole card: every page of the work gets the same event.
  std::vector<uint64_t> fps;
  if (a == 'R') {  // restore the whole archived work (archived pages belong to no card)
    std::string key, k2;
    int page = 0;
    const bool pix = g_cfg.groupWorks && pixivPage(baseName(items[idx].rel), key, page);
    for (const Item& it : items)
      if (it.archived && it.present && it.folder == items[idx].folder &&
          (pix ? pixivPage(baseName(it.rel), k2, page) && k2 == key : &it == &items[idx]))
        fps.push_back(it.fp);
    if (fps.empty()) fps.push_back(items[idx].fp);
  } else if (items[idx].work >= 0) {
    for (int p : works[items[idx].work].pages) fps.push_back(items[p].fp);
  } else {
    fps.push_back(items[idx].fp);
  }
  // A correction revokes exactly the events created by the rating it replaces, and re-rates
  // exactly the files that rating covered (the card may have been regrouped or archived since).
  std::vector<CidRef> targets;
  if (!replaceCid.empty()) {
    auto r = cidMap.find(replaceCid);
    if (r != cidMap.end()) {
      targets = r->second;
    } else if (replaceCid == "latest" || replaceCid.rfind("tick-", 0) == 0) {
      // "latest": the card's newest rating at the moment this request is applied (the gallery's full
      // view corrects that one; requests arrive in order, so quick corrections replace one another,
      // and a change made on another device is taken into account). "tick-<n>": a rating from before
      // ids, named by its tick (apiHistory's "ref"). Either way only this card's own.
      uint64_t t = 0;
      if (replaceCid == "latest") {
        for (const Event& e : items[idx].ev)
          if (e.a == 'L' || e.a == 'S' || e.a == 'D' || e.a == 'A') t = std::max(t, e.tick + 1);
        if (t) t--;  // none: nothing to replace (tick 0 never matches a rating below)
        else t = UINT64_MAX;
      } else {
        t = std::strtoull(replaceCid.c_str() + 5, nullptr, 10);
      }
      for (uint64_t fp : fps)
        for (const Event& e : items[byFp[fp]].ev)
          if (e.tick == t && (e.a == 'L' || e.a == 'S' || e.a == 'D' || e.a == 'A')) {
            targets.push_back({fp, t});
            break;
          }
    }
  }
  for (const CidRef& t : targets)
    if (std::find(fps.begin(), fps.end(), t.fp) == fps.end() && byFp.count(t.fp)) fps.push_back(t.fp);
  const uint64_t evTick = tick++;
  const int64_t ms = nowMs();
  std::string lines;
  for (const CidRef& t : targets) {
    auto f = byFp.find(t.fp);
    std::string abs = f == byFp.end() ? std::string() : absPathOf(items[f->second]);
    lines += std::to_string(evTick) + "\t" + std::to_string(ms) + "\t" + hex64(t.fp) + "\tX\t" + abs +
             "\ttarget=" + std::to_string(t.tick) + "\n";
  }
  for (uint64_t fp : fps) {
    std::string abs = absPathOf(items[byFp[fp]]);
    lines += std::to_string(evTick) + "\t" + std::to_string(ms) + "\t" + hex64(fp) + "\t" + a + "\t" + abs +
             (cid.empty() ? "" : "\tcid=" + cid) + "\n";
  }

  std::unique_lock<std::mutex> jl(jmu);  // keeps journal order == tick order
  lk.unlock();
  if (g_journalHook) g_journalHook();
  bool saved = appendJournal(lines);
  jl.unlock();
  lk.lock();
  if (!saved) {
    logLine("ERROR: could not write ratings.log");
    status = 500;
    return "{\"error\":\"could not save\"}";
  }

  // items may have grown or been regrouped while unlocked; fingerprints and ids stay valid
  idx = byFp[fps[0]];
  std::vector<std::string> chain;  // by path: an archive rebuilds (and renumbers) the folders
  std::vector<double> before;
  std::vector<int> rankBefore;
  for (int f = items[idx].folder; f >= 0; f = folders[f].parent) {
    chain.push_back(folders[f].path);
    before.push_back(folders[f].score);
    rankBefore.push_back(folders[f].rank);
  }
  bool replaced = false;
  bool visibility = a == 'A' || a == 'R';  // archive changes which images form cards
  for (const CidRef& t : targets) {
    auto f = byFp.find(t.fp);
    if (f == byFp.end()) continue;
    Item& it = items[f->second];
    for (size_t k = it.ev.size(); k-- > 0;)
      if (it.ev[k].tick == t.tick) {
        visibility |= it.ev[k].a == 'A' || it.ev[k].a == 'R';
        it.ev.erase(it.ev.begin() + (ptrdiff_t)k);
        replaced = true;
        break;
      }
    computeItem(it);
  }
  for (uint64_t fp : fps) {
    Item& it = items[byFp[fp]];
    Event ev{evTick, ms, a};
    it.ev.insert(std::upper_bound(it.ev.begin(), it.ev.end(), ev,
                                  [](const Event& x, const Event& y) { return x.tick < y.tick; }),
                 ev);
    if (!cid.empty()) cidMap[cid].push_back({fp, evTick});
    computeItem(it);
  }
  if (!replaced && a != 'R') sessionSwipes++;
  if (visibility)
    rebuildFolders();
  else
    computeFolders();
  version++;

  std::string o = "{\"saved\":true,\"tick\":" + std::to_string(evTick) + ",\"replaced\":" +
                  (replaced ? "true" : "false") + ",\"pages\":" + std::to_string(fps.size()) +
                  ",\"item\":" + cardJson(idx) + ",\"chain\":[";
  for (size_t k = 0; k < chain.size(); ++k) {
    auto fi = folderByPath.find(chain[k]);
    if (fi == folderByPath.end()) continue;  // the folder has no cards left
    const Folder& f = folders[fi->second];
    if (o.back() != '[') o += ',';
    o += "{\"path\":" + jsonStr(f.path) + ",\"name\":" + jsonStr(f.name) + ",\"depth\":" + std::to_string(f.depth) +
         ",\"before\":" + fmtNum(before[k], 2) + ",\"after\":" + fmtNum(f.score, 2) +
         ",\"rankBefore\":" + std::to_string(rankBefore[k]) + ",\"rank\":" + std::to_string(f.rank) +
         ",\"rated\":" + std::to_string(f.rated) + ",\"total\":" + std::to_string(f.total) +
         (isArtist(f) ? ",\"artist\":true" : "") + "}";
  }
  o += "]}";
  return o;
}

namespace {

// Writes revocations (journal "X" lines, as a correction does) for the given events, then drops
// them from memory. Call with `lk` holding `mu`; it is released while the journal is written.
bool revokeEvents(std::unique_lock<std::mutex>& lk, const std::vector<CidRef>& targets) {
  const uint64_t evTick = tick++;
  const int64_t ms = nowMs();
  std::string lines;
  for (const CidRef& t : targets) {
    auto f = byFp.find(t.fp);
    std::string abs = f == byFp.end() ? std::string() : absPathOf(items[f->second]);
    lines += std::to_string(evTick) + "\t" + std::to_string(ms) + "\t" + hex64(t.fp) + "\tX\t" + abs +
             "\ttarget=" + std::to_string(t.tick) + "\n";
  }
  std::unique_lock<std::mutex> jl(jmu);  // keeps journal order == tick order
  lk.unlock();
  bool saved = appendJournal(lines);
  jl.unlock();
  lk.lock();
  if (!saved) {
    logLine("ERROR: could not write ratings.log");
    return false;
  }
  bool visibility = false;
  for (const CidRef& t : targets) {
    auto f = byFp.find(t.fp);
    if (f == byFp.end()) continue;
    Item& it = items[f->second];
    for (size_t k = it.ev.size(); k-- > 0;)
      if (it.ev[k].tick == t.tick) {
        visibility |= it.ev[k].a == 'A' || it.ev[k].a == 'R';
        it.ev.erase(it.ev.begin() + (ptrdiff_t)k);
        break;
      }
    computeItem(it);
  }
  if (visibility)
    rebuildFolders();
  else
    computeFolders();
  baselineDirty = true;  // the session's starting ranks are recomputed from what is left
  version++;
  return true;
}

}  // namespace

std::string apiRevoke(const std::string& cid, int& status) {
  std::lock_guard<std::mutex> one(rateMu);  // see core_internal.h
  std::unique_lock<std::mutex> lk(mu);
  if (cid.empty() || !validCid(cid)) {
    status = 400;
    return "{\"error\":\"bad request\"}";
  }
  auto r = cidMap.find(cid);
  if (r == cidMap.end()) return "{\"revoked\":0}";
  // only the events that still exist (a later correction may have revoked some already)
  std::vector<CidRef> targets;
  for (const CidRef& t : r->second) {
    auto f = byFp.find(t.fp);
    if (f == byFp.end()) continue;
    for (const Event& e : items[f->second].ev)
      if (e.tick == t.tick) targets.push_back(t);
  }
  if (targets.empty()) {
    cidMap.erase(r);
    return "{\"revoked\":0}";
  }
  const int idx = byFp[targets[0].fp];
  if (!revokeEvents(lk, targets)) {
    status = 500;
    return "{\"error\":\"could not save\"}";
  }
  cidMap.erase(cid);
  return "{\"revoked\":" + std::to_string(targets.size()) + ",\"item\":" + itemJson(idx, "") + "}";
}

std::string apiResetRatings(const std::string& scope, bool dryRun, int& status) {
  std::lock_guard<std::mutex> one(rateMu);  // see core_internal.h
  std::unique_lock<std::mutex> lk(mu);
  if (scope.empty()) {  // never "everything" by accident
    status = 400;
    return "{\"error\":\"no folder\"}";
  }
  auto m = scopeMask(scope);
  std::vector<CidRef> targets;
  int images = 0;
  for (const Item& it : items) {
    if (it.folder < 0 || !m[it.folder]) continue;
    bool any = false;
    for (const Event& e : it.ev)
      if (e.a == 'L' || e.a == 'S' || e.a == 'D' || e.a == 'K') targets.push_back({it.fp, e.tick}), any = true;
    images += any;
  }
  const std::string counts = "\"images\":" + std::to_string(images) + ",\"ratings\":" + std::to_string(targets.size());
  if (dryRun || targets.empty()) return "{" + counts + "}";
  if (!revokeEvents(lk, targets)) {
    status = 500;
    return "{\"error\":\"could not save\"}";
  }
  logLine("reset: the ratings of " + std::to_string(images) + " images were revoked");
  return "{\"reset\":true," + counts + "}";
}

namespace {

std::vector<std::pair<uint64_t, int64_t>> picks;  // fingerprint, when; oldest first
bool picksLoaded = false;

std::wstring picksPath() { return g_dataDir + L"\\picks.tsv"; }

void loadPicks() {
  if (picksLoaded) return;
  picksLoaded = true;
  std::string text;
  if (!readFileBytes(picksPath(), text)) return;
  for (auto& line : split(text, '\n')) {
    auto f = split(line, '\t');
    if (f.empty() || f[0].size() != 16) continue;
    const uint64_t fp = std::strtoull(f[0].c_str(), nullptr, 16);
    if (fp) picks.push_back({fp, f.size() > 1 ? std::strtoll(f[1].c_str(), nullptr, 10) : 0});
  }
}

bool savePicks() {
  std::string out;
  for (auto& p : picks) out += hex64(p.first) + "\t" + std::to_string(p.second) + "\n";
  return writeFileAtomic(picksPath(), out);
}

}  // namespace

std::string apiPicks() {
  std::lock_guard<std::mutex> lk(mu);
  loadPicks();
  std::string o = "{\"items\":[";
  bool first = true;
  for (auto p = picks.rbegin(); p != picks.rend(); ++p) {
    auto f = byFp.find(p->first);
    if (f == byFp.end() || !items[f->second].present) continue;  // moved or deleted: kept, not shown
    if (!first) o += ',';
    first = false;
    o += itemJson(f->second, "");
  }
  return o + "]}";
}

std::string apiPick(const std::string& fpHex, bool add, int& status) {
  std::lock_guard<std::mutex> lk(mu);
  const uint64_t fp = fpHex.size() == 16 ? std::strtoull(fpHex.c_str(), nullptr, 16) : 0;
  if (!fp) {
    status = 400;
    return "{\"error\":\"bad request\"}";
  }
  loadPicks();
  auto at = std::find_if(picks.begin(), picks.end(), [&](const auto& p) { return p.first == fp; });
  if (add && at == picks.end()) picks.push_back({fp, nowMs()});
  else if (!add && at != picks.end()) picks.erase(at);
  else return "{\"ok\":true,\"count\":" + std::to_string(picks.size()) + "}";
  if (!savePicks()) {
    status = 500;
    return "{\"error\":\"could not save\"}";
  }
  return "{\"ok\":true,\"count\":" + std::to_string(picks.size()) + "}";
}

void coreForgetPicksForTest() {
  std::lock_guard<std::mutex> lk(mu);
  picks.clear();
  picksLoaded = false;
}

// One picture's ratings in order (withdrawn ones are gone already), each with the points it had
// right after it, replayed as computeItem does, and how to name it for a correction (/api/rate's
// replace): the id of the rating it came from, or "tick-<n>" for one from before ids.
// {"events":[{"a":"L","ms":..,"p":67,"ref":".."},...]}
std::string apiHistory(int id, const std::string& fpHex) {
  std::lock_guard<std::mutex> lk(mu);
  const int idx = resolveItem(id, fpHex);
  if (idx < 0) return "{\"events\":[]}";
  const Item& it = items[idx];
  std::unordered_map<uint64_t, const std::string*> cidOfTick;
  for (const auto& [c, refs] : cidMap)
    for (const CidRef& r : refs)
      if (r.fp == it.fp) cidOfTick[r.tick] = &c;
  double p = 0;
  int nL = 0, nS = 0;
  std::string o = "{\"events\":[";
  bool first = true;
  for (const Event& e : it.ev) {
    if (e.a != 'L' && e.a != 'S' && e.a != 'D' && e.a != 'A') continue;
    if (e.a == 'L') {
      p = nL + nS == 0 ? kPtsLike : p + (nS ? kAddLikeOnSuper : kAddLike);
      nL++;
    } else if (e.a == 'S') {
      p = nS ? p + kAddSuper : std::max(kPtsSuper, p + kAddSuper);
      nS++;
    }
    o += std::string(first ? "" : ",") + "{\"a\":\"" + e.a + "\",\"ms\":" + std::to_string(e.ms) + ",\"p\":" + fmtNum(p, 0);
    auto c = cidOfTick.find(e.tick);
    o += ",\"ref\":" + jsonStr(c != cidOfTick.end() ? *c->second : "tick-" + std::to_string(e.tick));
    o += "}";
    first = false;
  }
  return o + "]}";
}
