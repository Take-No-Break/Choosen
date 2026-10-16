// Scheduling ("reverse SRS"), image points, folder scores and ranks, and choosing the next cards.
#include "core_internal.h"

namespace core {

uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

// Deterministic +-15% spread so cards rated together do not all return together.
double jitter(uint64_t fp, int k) {
  uint64_t h = mix64(fp ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(k + 1)));
  return 0.85 + 0.30 * ((double)(h >> 11) / 9007199254740992.0);
}

void computeItem(Item& it) {
  it.touched = false;
  it.rated = false;
  it.hold = false;
  it.archived = false;
  it.S = 1;
  it.due = 0;
  it.lastRateTick = 0;
  it.nRatings = 0;
  it.nLike = 0;
  it.nSuper = 0;
  it.last = 0;
  it.lastMs = 0;
  it.p = 0;
  int lastCat = -1, k = 0;
  uint64_t prevTick = 0, prevDue = 0;
  for (const Event& e : it.ev) {
    if (e.a == 'A' || e.a == 'R') {  // archive / restore: visibility only, the rating is kept
      it.archived = e.a == 'A';
      continue;
    }
    it.touched = true;
    it.lastMs = e.ms;
    if (e.a == 'K') {  // hold: back in about 30 cards, rating unchanged
      it.due = e.tick + (uint64_t)(kSkipInterval * jitter(it.fp, 1000 + k++));
      it.hold = true;
      continue;
    }
    it.hold = false;
    int cat = e.a == 'D' ? 0 : 1;
    double g = e.a == 'S' ? kGrowSuper : e.a == 'L' ? kGrowLike : kGrowPass;
    if (it.nRatings) {
      // Early review (before the planned interval ran out): grow only in proportion to the part
      // of the interval that passed. Late review (reviews were piling up): credit the extra time
      // the image actually went unseen, so a busy schedule spreads itself out on its own.
      double planned = (double)std::max<uint64_t>(1, prevDue - prevTick);
      double frac = e.tick > prevTick ? (double)(e.tick - prevTick) / planned : 0.0;
      if (frac < 1.0) g = 1.0 + (g - 1.0) * std::max(0.0, frac);
      else it.S *= std::min(frac, g_policy.lateCap);
    }
    if (lastCat == 0 && cat == 1) it.S = std::max(1.0, it.S * kFlipKeep);
    it.S = std::min(it.S * g, kMaxStability);
    it.due = e.tick + (uint64_t)(it.S * kBaseInterval * jitter(it.fp, k++));
    prevTick = e.tick;
    prevDue = it.due;
    lastCat = cat;
    it.last = e.a;
    it.lastRateTick = e.tick;
    it.nRatings++;
    if (e.a == 'L') {
      it.p = it.nLike + it.nSuper == 0 ? kPtsLike : it.p + (it.nSuper ? kAddLikeOnSuper : kAddLike);
      it.nLike++;
    } else if (e.a == 'S') {
      it.p = it.nSuper ? it.p + kAddSuper : std::max(kPtsSuper, it.p + kAddSuper);
      it.nSuper++;
    }
  }
  it.rated = it.nRatings > 0;
}

// Lower bound of the right-swipe share (Wilson score interval). A like always raises it, a pass
// always lowers it, and a folder needs several likes before it can outrank a well-sampled one:
// 2 of 2 -> 43, 5 of 5 -> 65, 60 of 100 -> 52, 90 of 100 -> 84.
double wilsonLower(int liked, int n) {
  if (n <= 0) return 0;
  const double z = kWilsonZ, z2 = z * z;
  const double p = (double)liked / n;
  return (p + z2 / (2.0 * n) - z * std::sqrt(p * (1 - p) / n + z2 / (4.0 * n * n))) / (1 + z2 / n);
}

// Ranks folders of equal depth by score, then super likes, then sample size.
void rankFolders(const std::vector<double>& score, const std::vector<int>& rated, const std::vector<int>& supers,
                 std::vector<int>& rankOut) {
  // Folders rank against the others of their depth; artist folders all rank together at the
  // artist depth, including a photo folder that is one artist because its subfolders are not read.
  int maxDepth = g_cfg.artistDepth;
  for (auto& f : folders) maxDepth = std::max(maxDepth, f.depth);
  std::vector<std::vector<int>> byDepth(maxDepth + 1);
  for (int i = 0; i < (int)folders.size(); ++i)
    if (rated[i] > 0) byDepth[isArtist(folders[i]) ? g_cfg.artistDepth : folders[i].depth].push_back(i);
  rankOut.assign(folders.size(), 0);
  for (auto& v : byDepth) {
    std::sort(v.begin(), v.end(), [&](int a, int b) {
      if (score[a] != score[b]) return score[a] > score[b];
      if (supers[a] != supers[b]) return supers[a] > supers[b];
      if (rated[a] != rated[b]) return rated[a] > rated[b];
      return folders[a].path < folders[b].path;
    });
    for (int r = 0; r < (int)v.size(); ++r) rankOut[v[r]] = r + 1;
  }
}

// The page that speaks for a work: the one whose latest rating is newest (the same rule the
// ranking baseline uses); among unrated pages, the one touched most recently (e.g. held); the
// cover otherwise. A hold goes to every page, so the chosen page carries the hold as well.
int leadOf(const Work& w) {
  int best = w.pages[0];
  bool bestRated = false;
  uint64_t bestRate = 0, bestTouch = 0;
  for (int i : w.pages) {
    const Item& it = items[i];
    if (it.ev.empty()) continue;
    const uint64_t touch = it.ev.back().tick;
    bool better;
    if (it.rated != bestRated) better = it.rated;
    else if (it.rated && it.lastRateTick != bestRate) better = it.lastRateTick > bestRate;
    else better = touch > bestTouch || items[best].ev.empty();
    if (better) best = i, bestRated = it.rated, bestRate = it.lastRateTick, bestTouch = touch;
  }
  return best;
}

void computeFolders() {
  const size_t n = folders.size();
  std::vector<int> rated(n, 0), liked(n, 0), supers(n, 0), total(n, 0), images(n, 0);
  for (auto& w : works) {
    if (w.folder < 0) continue;
    const Item& it = items[leadOf(w)];
    for (int f = w.folder; f >= 0; f = folders[f].parent) {
      total[f]++;
      images[f] += (int)w.pages.size();
      if (it.rated) {
        rated[f]++;
        liked[f] += it.last != 'D';
        supers[f] += it.last == 'S';
      }
    }
  }
  std::vector<double> score(n);
  for (size_t i = 0; i < n; ++i) score[i] = 100.0 * wilsonLower(liked[i], rated[i]);
  std::vector<int> rank;
  rankFolders(score, rated, supers, rank);
  for (size_t i = 0; i < n; ++i) {
    Folder& F = folders[i];
    F.total = total[i];
    F.images = images[i];
    F.rated = rated[i];
    F.liked = liked[i];
    F.supers = supers[i];
    F.score = score[i];
    F.rank = rank[i];
  }
}

// Folder scores and ranks as they were at an earlier moment, recomputed from the ratings made
// before it (by tick for "this session", by wall-clock time for "yesterday", "last week" ...).
// Recomputing instead of snapshotting keeps the ▲▼ arrows right even when the first scan ends
// after swiping began.
void ranksBefore(bool byTime, uint64_t limit, std::vector<int>& rank, std::vector<double>& score,
                 std::vector<int>& rated) {
  const size_t n = folders.size();
  std::vector<int> liked(n, 0), supers(n, 0);
  rated.assign(n, 0);
  for (auto& w : works) {
    if (w.folder < 0) continue;
    char last = 0;
    uint64_t lastTick = 0;
    for (int i : w.pages)
      for (auto e = items[i].ev.rbegin(); e != items[i].ev.rend(); ++e)
        if ((byTime ? (uint64_t)e->ms : e->tick) < limit && (e->a == 'L' || e->a == 'S' || e->a == 'D')) {
          if (!last || e->tick > lastTick) last = e->a, lastTick = e->tick;
          break;
        }
    if (!last) continue;
    for (int f = w.folder; f >= 0; f = folders[f].parent) {
      rated[f]++;
      liked[f] += last != 'D';
      supers[f] += last == 'S';
    }
  }
  score.assign(n, 0);
  for (size_t i = 0; i < n; ++i) score[i] = 100.0 * wilsonLower(liked[i], rated[i]);
  rankFolders(score, rated, supers, rank);
}

void computeBaseline() {
  std::vector<int> rank, rated;
  std::vector<double> score;
  ranksBefore(false, sessionTick0, rank, score, rated);
  for (size_t i = 0; i < folders.size(); ++i) folders[i].rank0 = rank[i];
  baselineDirty = false;
}

// Which class a card belongs to, by its latest swipe (the weighted score p is only for sorting).
bool cardMatches(const std::string& filter, const Item& it) {
  if (filter.empty() || filter == "all") return true;
  if (filter == "liked") return it.rated && it.last != 'D';
  if (filter == "likeOnly") return it.rated && it.last == 'L';  // likes without the super likes
  if (filter == "super") return it.rated && it.last == 'S';
  if (filter == "passed") return it.rated && it.last == 'D';
  if (filter == "unrated") return !it.rated;
  if (filter == "rated") return it.rated;
  return false;
}

}  // namespace core

using namespace core;

// Picks the next cards. Normal order of preference:
//  1. held cards whose ~30-card wait is over
//  2. reviews, taking the review share of the cards (0 = none while new images remain,
//     1 = reviews only); the most overdue relative to its own interval goes first and overdue
//     passes wait behind overdue likes
//  3. new images in random order
//  4. when nothing is due: the next images to become due ("ahead"), then the ones just seen
// filter "fresh" = unrated cards only; "liked" / "likeOnly" / "super" / "passed" = re-swipe cards of that class
// (curating), least recently rated first so every card gets its turn before any comes round again.
// exclude = ids already in the client's queue (never returned); recent = ids just shown (used last).
std::string apiNext(const std::string& scope, const std::string& exclude, const std::string& recent, int n,
                    const std::string& filter) {
  std::lock_guard<std::mutex> lk(mu);
  n = std::clamp(n, 1, 50);
  auto m = scopeMask(scope);
  std::unordered_set<int> ex, rec;
  for (auto& s : split(exclude, ','))
    if (!s.empty()) ex.insert(atoi(s.c_str()));
  for (auto& s : split(recent, ','))
    if (!s.empty()) rec.insert(atoi(s.c_str()));
  struct Cand {
    double key;
    int id;
    bool operator<(const Cand& o) const { return key != o.key ? key < o.key : id < o.id; }
  };
  const bool curate = filter == "liked" || filter == "likeOnly" || filter == "super" || filter == "passed";
  const bool freshOnly = filter == "fresh";
  const double share = std::clamp(g_policy.reviewShareMin, 0.0, 1.0);
  const bool reviewsOnly = share >= 0.999;
  std::vector<Cand> holds, due, fresh, ahead, lastResort, pool;
  int dueCount = 0, freshCount = 0, holdCount = 0, total = 0;
  for (const Work& W : works) {  // one card per work; the card is named by its cover page
    if (W.folder < 0 || !m[W.folder]) continue;
    const int i = W.pages[0];
    const Item& it = items[leadOf(W)];
    if (curate) {
      if (!cardMatches(filter, it)) continue;
      total++;
      if (ex.count(i)) continue;
      (rec.count(i) ? lastResort : pool).push_back({(double)it.lastRateTick, i});
      continue;
    }
    if (freshOnly && it.rated) continue;
    total++;
    const bool isDue = it.touched && it.due <= tick;
    if (!it.touched) freshCount++;
    else if (isDue && it.hold) holdCount++;
    else if (isDue) dueCount++;
    if (ex.count(i)) continue;
    if (rec.count(i)) {
      lastResort.push_back({(double)it.due, i});
    } else if (!it.touched) {
      if (!reviewsOnly) fresh.push_back({(double)(mix64(items[i].fp ^ sessionSeed) >> 11), i});
    } else if (isDue && it.hold) {
      holds.push_back({(double)it.due, i});
    } else if (isDue) {
      double planned = std::max(1.0, (double)(it.due - it.lastRateTick));
      double overdue = (double)(tick - it.due + 1) / planned;
      if (it.last == 'D') overdue *= g_policy.passWeight;
      due.push_back({-overdue, i});
    } else if (!freshOnly) {
      ahead.push_back({(double)it.due, i});
    }
  }
  auto smallest = [n](std::vector<Cand>& v) {
    size_t k = std::min<size_t>(v.size(), (size_t)n);
    std::partial_sort(v.begin(), v.begin() + k, v.end());
    v.resize(k);
  };
  smallest(holds);
  smallest(due);
  smallest(fresh);
  smallest(ahead);
  smallest(lastResort);
  smallest(pool);

  std::string o = "{\"items\":[";
  size_t hi = 0, di = 0, fi = 0, ai = 0, li = 0, pi = 0;
  for (int k = 0; k < n; ++k) {
    const double t = (double)(tick + (uint64_t)k);
    const bool wantReview = !freshOnly && std::floor((t + 1) * share) > std::floor(t * share);
    int pick;
    const char* kind;
    if (curate) {
      if (pi < pool.size()) pick = pool[pi++].id, kind = "curate";
      else if (li < lastResort.size()) pick = lastResort[li++].id, kind = "curate";
      else break;
    } else if (hi < holds.size()) {
      pick = holds[hi++].id, kind = "hold";
    } else if (wantReview && di < due.size()) {
      pick = due[di++].id, kind = "review";
    } else if (fi < fresh.size()) {
      pick = fresh[fi++].id, kind = "new";
    } else if (!freshOnly && di < due.size()) {
      pick = due[di++].id, kind = "review";
    } else if (ai < ahead.size()) {
      pick = ahead[ai++].id, kind = "ahead";
    } else if (li < lastResort.size()) {
      pick = lastResort[li++].id, kind = "ahead";
    } else {
      break;
    }
    if (k) o += ',';
    const int w = items[pick].work;
    o += itemJson(pick, kind, leadOf(works[w]), &works[w].pages);
  }
  o += "],\"counts\":{\"due\":" + std::to_string(dueCount + holdCount) + ",\"hold\":" + std::to_string(holdCount) +
       ",\"fresh\":" + std::to_string(freshCount) + ",\"total\":" + std::to_string(total) +
       ",\"reviewShare\":" + fmtNum(share, 2) + "}}";
  return o;
}
