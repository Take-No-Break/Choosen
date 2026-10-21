// Developer tools: --selftest, --bench, --simulate. Everything happens in %TEMP%; data\ is never touched.
#include "selftest.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "win.h"
#include "api_routes.h"
#include "common.h"
#include "core.h"
#include "platform.h"
#include "remote.h"
#include "qr.h"

namespace {

std::string g_out;
int g_pass = 0, g_fail = 0;

void say(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  g_out += buf;
  g_out += "\n";
}

#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (cond) {                                           \
      g_pass++;                                           \
    } else {                                              \
      g_fail++;                                           \
      say("  FAIL line %d: %s", __LINE__, #cond);         \
      say("       " __VA_ARGS__);                         \
    }                                                     \
  } while (0)

bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

double nowSec() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)f.QuadPart;
}

std::wstring tempRoot(const wchar_t* tag) {
  wchar_t b[MAX_PATH];
  GetTempPathW(MAX_PATH, b);
  return std::wstring(b) + L"choosen-" + tag + L"-" + std::to_wstring(GetCurrentProcessId());
}

void rmTree(const std::wstring& dir) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      std::wstring n = fd.cFileName;
      if (n == L"." || n == L"..") continue;
      std::wstring p = dir + L"\\" + n;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rmTree(p);
      else {
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(p.c_str());
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

void mkdirs(const std::wstring& path) {
  for (size_t i = 3; i <= path.size(); ++i)
    if (i == path.size() || path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
}

void writeRandom(const std::wstring& path, size_t size, uint32_t seed) {
  size_t slash = path.find_last_of(L'\\');
  mkdirs(path.substr(0, slash));
  std::mt19937 r(seed);
  std::string s(size, '\0');
  for (auto& c : s) c = (char)(r() & 0xff);
  writeFileAtomic(path, s);
}

struct Rated {
  int status = 0;
  std::string body;
};

Rated rate(const std::string& rel, char a, const std::string& cid, const std::string& replace = "") {
  DebugItem d;
  Rated r;
  if (!coreDebugItem(rel, d)) {
    r.status = -1;
    return r;
  }
  r.status = 200;
  r.body = apiRate(d.id, d.fp, a, cid, replace, r.status);
  return r;
}

DebugItem item(const std::string& rel) {
  DebugItem d;
  coreDebugItem(rel, d);
  return d;
}

DebugFolder folder(const std::string& path) {
  DebugFolder f;
  f.total = -1;
  coreDebugFolder(path, f);
  return f;
}

void restart() {
  coreResetForTest();
  coreInit();
  coreScanSync();
}

void setGroup(int on) {
  ConfigPatch c;
  c.groupWorks = on;
  coreSetConfig(c);
}

// ---------------------------------------------------------------------------------------------
int selftest() {
  std::wstring base = tempRoot(L"selftest");
  rmTree(base);
  std::wstring lib = base + L"\\lib", data = base + L"\\data";
  mkdirs(data);
  g_dataDir = data;
  g_cfg = Config{};
  g_cfg.root = narrow(lib);

  uint32_t seed = 1;
  auto put = [&](const wchar_t* rel, size_t size) { writeRandom(lib + L"\\" + rel, size, seed++); };
  put(L"A (1)\\a1.jpg", 20000);
  put(L"A (1)\\a2.jpg", 5000);  // smaller than one fingerprint chunk
  put(L"A (1)\\a3.png", 30000);
  put(L"A (1)\\a4.jpg", 12000);
  put(L"A (1)\\a5.jpg", 9000);
  put(L"A (1)\\a6.jpg", 40000);
  CopyFileW((lib + L"\\A (1)\\a1.jpg").c_str(), (lib + L"\\A (1)\\dup.jpg").c_str(), FALSE);
  writeFileAtomic(lib + L"\\A (1)\\notes.txt", "not an image");
  put(L"B (2)\\b1.png", 15000);
  put(L"B (2)\\b2.jpg", 16000);
  put(L"B (2)\\b3.jpg", 17000);
  put(L"B (2)\\b4.jpg", 18000);
  put(L"C (3)\\sub\\c1.jpg", 11000);
  put(L"C (3)\\sub\\c2.jpg", 11500);
  put(L"C (3)\\sub\\c3.jpg", 11700);
  put(L"top.jpg", 8000);
  put(L".venv\\pkg\\x.jpg", 3000);
  put(L"__pycache__\\y.png", 3000);
  put(L"みどり (4)\\m1.jpg", 9100);

  say("== scan");
  restart();
  CHECK(folder("").total == 15, "root total = %d (15 expected: duplicate counted once, ignored folders skipped)", folder("").total);
  CHECK(folder("A (1)").total == 6, "A total = %d", folder("A (1)").total);
  CHECK(folder("C (3)").total == 3 && folder("C (3)").depth == 1, "C total/depth = %d/%d", folder("C (3)").total,
        folder("C (3)").depth);
  CHECK(folder("C (3)/sub").total == 3 && folder("C (3)/sub").depth == 2, "C/sub");
  CHECK(folder("みどり (4)").total == 1, "unicode folder");
  CHECK(!item(".venv/pkg/x.jpg").present && !item("__pycache__/y.png").present, "ignored folders were indexed");
  CHECK(item("A (1)/a1.jpg").present != item("A (1)/dup.jpg").present, "exactly one of two identical files is indexed");

  say("== rating and scheduling");
  CHECK(rate("A (1)/a2.jpg", 'L', "c1").status == 200, "like");
  CHECK(rate("A (1)/a3.png", 'D', "c2").status == 200, "pass");
  CHECK(rate("B (2)/b1.png", 'S', "c3").status == 200, "super");
  CHECK(rate("C (3)/sub/c1.jpg", 'D', "c4").status == 200, "pass in subfolder");
  CHECK(rate("top.jpg", 'L', "c5").status == 200, "like at root level");
  DebugItem a2 = item("A (1)/a2.jpg"), a3 = item("A (1)/a3.png"), b1 = item("B (2)/b1.png");
  CHECK(a3.S > b1.S && b1.S > a2.S, "stability pass > super > like: %.2f %.2f %.2f", a3.S, b1.S, a2.S);
  CHECK(a3.dueIn > a2.dueIn, "a pass comes back later than a like: %lld vs %lld", (long long)a3.dueIn, (long long)a2.dueIn);
  CHECK(a2.dueIn > 250 && a2.dueIn < 500, "first like interval ~375 cards: %lld", (long long)a2.dueIn);
  CHECK(a2.p == 67 && b1.p == 86 && a3.p == 0, "points: like 67, super 86, pass 0: %.0f %.0f %.0f", a2.p, b1.p, a3.p);
  CHECK(folder("A (1)").rated == 2 && folder("A (1)").liked == 1, "A rated/liked = %d/%d", folder("A (1)").rated,
        folder("A (1)").liked);
  CHECK(folder("C (3)").rated == 1 && folder("C (3)/sub").rated == 1, "subfolder rating reaches the artist folder");
  CHECK(folder("B (2)").rank == 1, "super-liked folder ranks first (rank %d)", folder("B (2)").rank);

  say("== retries, corrections and validation");
  Rated again = rate("A (1)/a2.jpg", 'L', "c1");
  CHECK(has(again.body, "\"duplicate\":true") && item("A (1)/a2.jpg").events == 1, "a retried POST must not add a rating");
  CHECK(rate("A (1)/a2.jpg", 'D', "c6", "c1").status == 200, "correction");
  a2 = item("A (1)/a2.jpg");
  CHECK(a2.events == 1 && a2.last == 'D' && a2.p == 0.0, "correction replaced the like: events %d last %c", a2.events,
        a2.last ? a2.last : '-');
  {
    const std::string h = apiHistory(a2.id, "");
    CHECK(has(h, "\"ref\":\"c6\"") && !has(h, "\"c1\""), "the history names the rating that can be corrected: %s", h.c_str());
  }
  {
    // a rating from before ids (no cid): named by its tick, and corrected by it
    const int e0 = item("A (1)/a3.png").events;
    CHECK(rate("A (1)/a3.png", 'L', "").status == 200, "a rating without an id");
    const std::string h = apiHistory(item("A (1)/a3.png").id, "");
    const size_t at = h.rfind("\"ref\":\"tick-");
    CHECK(at != std::string::npos, "an old rating is named by its tick: %s", h.c_str());
    const std::string ref = at == std::string::npos ? "" : h.substr(at + 7, h.find('"', at + 7) - (at + 7));
    CHECK(rate("A (1)/a3.png", 'S', "c-legacy", ref).status == 200, "correcting it by its tick");
    DebugItem a3 = item("A (1)/a3.png");
    CHECK(a3.events == e0 + 1 && a3.last == 'S', "the old rating was replaced, not added to: events %d (want %d), last %c",
          a3.events, e0 + 1, a3.last ? a3.last : '-');
    CHECK(rate("A (1)/a3.png", 'L', "c-other", "tick-999999").status == 200 && item("A (1)/a3.png").events == e0 + 2,
          "a tick that is not this picture's replaces nothing");
    // "latest": the newest rating when the request is applied, so two quick corrections chain
    CHECK(rate("A (1)/a3.png", 'S', "c-latest1", "latest").status == 200 && rate("A (1)/a3.png", 'D', "c-latest2", "latest").status == 200,
          "two corrections of the latest rating");
    DebugItem z = item("A (1)/a3.png");
    CHECK(z.events == e0 + 2 && z.last == 'D', "each replaced the newest one: events %d (want %d), last %c", z.events, e0 + 2,
          z.last ? z.last : '-');
    CHECK(has(rate("A (1)/a3.png", 'D', "c-latest2", "latest").body, "\"duplicate\":true") && item("A (1)/a3.png").events == e0 + 2,
          "a retried correction is not applied twice");
    // two devices correct the same picture at once: the second starts while the first is being
    // written to the journal, and must not pick the same rating to replace
    static std::thread* second = nullptr;
    coreDebugJournalHook([] {
      coreDebugJournalHook(nullptr);  // only the first write waits
      second = new std::thread([] { rate("A (1)/a3.png", 'L', "c-race2", "latest"); });
      std::this_thread::sleep_for(std::chrono::milliseconds(150));  // the second is under way meanwhile
    });
    const Rated first = rate("A (1)/a3.png", 'S', "c-race1", "latest");
    if (second) second->join();
    delete second;
    second = nullptr;
    coreDebugJournalHook(nullptr);
    DebugItem race = item("A (1)/a3.png");
    CHECK(first.status == 200 && race.events == e0 + 2 && race.last == 'L',
          "concurrent corrections of the latest rating replace one another: events %d (want %d), last %c", race.events,
          e0 + 2, race.last ? race.last : '-');
  }
  CHECK(rate("A (1)/a4.jpg", 'L', "c7", "no-such-cid").status == 200 && item("A (1)/a4.jpg").events == 1,
        "correcting an unknown rating just adds the new one");
  {
    DebugItem b2 = item("B (2)/b2.jpg"), a5 = item("A (1)/a5.jpg");
    int st = 200;
    apiRate(b2.id, a5.fp, 'L', "c8", "", st);  // stale id, correct fingerprint
    CHECK(st == 200 && item("A (1)/a5.jpg").events == 1 && item("B (2)/b2.jpg").events == 0,
          "the fingerprint decides which photo is rated, not the id");
    st = 200;
    apiRate(b2.id, "0123456789abcdef", 'L', "c9", "", st);
    CHECK(st == 404, "unknown fingerprint rejected (status %d)", st);
    st = 200;
    apiRate(b2.id, b2.fp, 'L', "bad\tcid", "", st);
    CHECK(st == 400, "cid with a tab rejected (status %d)", st);
    st = 200;
    apiRate(b2.id, b2.fp, 'Q', "c10", "", st);
    CHECK(st == 400, "unknown action rejected");
  }
  CHECK(rate("A (1)/a5.jpg", 'D', "c11").status == 200, "re-rate right away");
  CHECK(item("A (1)/a5.jpg").S < 2.6, "an early re-rating barely grows the interval: S = %.2f", item("A (1)/a5.jpg").S);
  {
    double before = folder("B (2)").score;
    rate("B (2)/b4.jpg", 'L', "c13");
    double afterLike = folder("B (2)").score;
    rate("B (2)/b3.jpg", 'D', "c14");
    double afterPass = folder("B (2)").score;
    CHECK(afterLike > before && afterPass < afterLike, "folder score: like raises, pass lowers (%.1f -> %.1f -> %.1f)",
          before, afterLike, afterPass);
  }
  {
    rate("C (3)/sub/c2.jpg", 'D', "c15");
    rate("C (3)/sub/c2.jpg", 'L', "c16");  // changed its mind: latest swipe decides the class
    std::string liked = apiItems("liked", "C (3)", "score", 0, 50);
    std::string passed = apiItems("passed", "C (3)", "score", 0, 50);
    CHECK(has(liked, "c2.jpg") && !has(passed, "c2.jpg"), "pass-then-like must be listed as liked");
    CHECK(has(apiItems("likeOnly", "C (3)", "score", 0, 50), "c2.jpg") && !has(apiItems("super", "C (3)", "score", 0, 50), "c2.jpg") &&
              has(apiNext("C (3)", "", "", 10, "likeOnly"), "c2.jpg"),
          "likeOnly lists and swipes the likes without the super likes");
  }

  say("== scheduler rules (on-time reviews)");
  {
    // replays actions, each one exactly when the previous one became due
    auto onTime = [](const std::string& acts) {
      std::vector<uint64_t> ticks;
      uint64_t t = 1000;
      DebugItem d;
      for (size_t i = 0; i < acts.size(); ++i) {
        ticks.push_back(t);
        d = coreDebugSchedule(acts.substr(0, i + 1), ticks);
        t += (uint64_t)std::max<int64_t>(1, d.dueIn);
      }
      return d;
    };
    auto iv = [&](const char* acts) { return onTime(acts).dueIn; };
    CHECK(iv("LD") > iv("LL"), "like then pass must come back later than like then like (%lld vs %lld)",
          (long long)iv("LD"), (long long)iv("LL"));
    CHECK(iv("SSD") > iv("SSL"), "super, super, pass later than super, super, like (%lld vs %lld)", (long long)iv("SSD"),
          (long long)iv("SSL"));
    CHECK(iv("DL") < iv("DD"), "pass then like (changed mind) comes back sooner than pass, pass");
    CHECK(iv("D") > iv("S") && iv("S") > iv("L"), "first intervals pass > super > like: %lld %lld %lld",
          (long long)iv("D"), (long long)iv("S"), (long long)iv("L"));
    CHECK(iv("LL") > iv("L") * 2, "a like on time grows the interval: %lld -> %lld", (long long)iv("L"), (long long)iv("LL"));
    DebugItem cap = onTime(std::string(40, 'D'));
    CHECK(cap.S <= 1e5 && cap.dueIn > 0 && cap.dueIn < 30000000, "stability is capped: S=%.0f dueIn=%lld", cap.S,
          (long long)cap.dueIn);
    DebugItem early = coreDebugSchedule("LL", {0, 10});
    CHECK(early.S < 2.7, "an early review grows only a little: S=%.2f", early.S);
    DebugItem held = coreDebugSchedule("LK", {0, 5});
    CHECK(held.last == 'L' && held.p == 67 && held.dueIn >= 25 && held.dueIn <= 35,
          "hold keeps the rating and returns in ~30 cards: last %c p %.2f due %lld", held.last ? held.last : '-', held.p,
          (long long)held.dueIn);
  }

  say("== points and like / super counts");
  {
    auto pts = [](const char* acts) {
      std::vector<uint64_t> ticks;
      for (size_t i = 0; acts[i]; ++i) ticks.push_back(1000 * (i + 1));
      return coreDebugSchedule(acts, ticks);
    };
    struct Case { const char* acts; double p; int nL, nS; };
    const Case cases[] = {
        {"L", 67, 1, 0},    {"LL", 69, 2, 0},  {"LLL", 71, 3, 0},  {"S", 86, 0, 1},    {"SL", 87, 1, 1},
        {"SS", 88, 0, 2},   {"SLL", 88, 2, 1}, {"LS", 86, 1, 1},   {"LLS", 86, 2, 1},  {"LSL", 87, 2, 1},
        {"D", 0, 0, 0},     {"LD", 67, 1, 0},  {"DL", 67, 1, 0},   {"LDL", 69, 2, 0},  {"SDS", 88, 0, 2},
        {"LLLLLLLLLLLL", 89, 12, 0}, {"LLLLLLLLLLLLS", 91, 12, 1}, {"LK", 67, 1, 0},
    };
    for (const Case& c : cases) {
      DebugItem d = pts(c.acts);
      CHECK(d.p == c.p && d.nL == c.nL && d.nS == c.nS, "%s -> %.0f points, %d likes, %d supers (want %.0f, %d, %d)",
            c.acts, d.p, d.nL, d.nS, c.p, c.nL, c.nS);
    }
    CHECK(has(apiItems("all", "B (2)", "score", 0, 10), "\"nS\":1"), "cards carry the super count");
    const std::string hist = apiHistory(item("B (2)/b1.png").id, "");
    CHECK(has(hist, "\"a\":\"S\"") && has(hist, "\"p\":86") && has(apiHistory(-5, ""), "\"events\":[]"),
          "a picture's history lists its ratings with the points after each: %s", hist.c_str());
    std::string rootOnly = apiItems("all", "/", "name", 0, 50);
    CHECK(has(rootOnly, "top.jpg") && !has(rootOnly, "a2.jpg"), "scope \"/\" = only the images directly in the photo folder");
  }

  say("== restart keeps everything");
  std::map<std::string, DebugItem> before;
  for (const char* rel : {"A (1)/a2.jpg", "A (1)/a3.png", "A (1)/a4.jpg", "A (1)/a5.jpg", "B (2)/b1.png",
                          "B (2)/b3.jpg", "B (2)/b4.jpg", "C (3)/sub/c1.jpg", "C (3)/sub/c2.jpg", "top.jpg"})
    before[rel] = item(rel);
  double scoreA = folder("A (1)").score;
  uint64_t tickBefore = coreDebugTick();
  restart();
  CHECK(coreDebugTick() == tickBefore, "tick %llu -> %llu", (unsigned long long)tickBefore,
        (unsigned long long)coreDebugTick());
  for (auto& kv : before) {
    DebugItem now = item(kv.first);
    CHECK(now.events == kv.second.events && now.p == kv.second.p && now.S == kv.second.S &&
              now.dueIn == kv.second.dueIn,
          "%s changed after restart", kv.first.c_str());
  }
  CHECK(folder("A (1)").score == scoreA, "folder score changed after restart");
  CHECK(has(rate("A (1)/a2.jpg", 'L', "c1").body, "\"duplicate\":true"), "cids survive a restart");

  say("== rename and move keep ratings");
  MoveFileW((lib + L"\\A (1)\\a4.jpg").c_str(), (lib + L"\\B (2)\\moved.jpg").c_str());
  coreScanSync();
  DebugItem mv = item("B (2)/moved.jpg");
  CHECK(mv.present && mv.events == 1 && mv.last == 'L', "moved file lost its rating");
  CHECK(folder("A (1)").total == 5 && folder("B (2)").total == 5, "totals after move: A %d B %d",
        folder("A (1)").total, folder("B (2)").total);

  say("== journal repair and legacy lines");
  std::wstring journal = data + L"\\ratings.log";
  appendFileBytes(journal, "999\t1\tdeadbeefdeadbeef\tL\tC:\\torn-line-without-newline");
  restart();
  CHECK(rate("B (2)/b2.jpg", 'L', "c12").status == 200, "rate after torn line");
  restart();
  CHECK(item("B (2)/b2.jpg").events == 1, "rating after a torn line was lost");
  {
    DebugItem b3 = item("B (2)/b3.jpg");
    std::string t = std::to_string(coreDebugTick() + 5);
    appendFileBytes(journal, t + "\t1\t" + b3.fp + "\tL\tx\n" + t + "\t2\t" + b3.fp + "\tX\tx\n");
    restart();
    CHECK(item("B (2)/b3.jpg").events == 1 && item("B (2)/b3.jpg").last == 'D',
          "v0.1 X line (no target) revokes only the latest rating");
  }

  say("== backups");
  CHECK(has(apiState(), "\"warning\":\"\""), "no warning in normal operation");
  {
    std::string full;
    readFileBytes(journal, full);
    writeFileAtomic(journal, full.substr(0, full.find('\n') + 1));  // simulate a truncated journal
    restart();
    CHECK(!has(apiState(), "\"warning\":\"\""), "a shrunken journal must raise a warning");
    writeFileAtomic(journal, full);
    restart();
  }

  say("== queries");
  for (const char* s : {"score", "folder", "recent", "name", "random"}) {
    std::string r = apiItems("all", "", s, 0, 100);
    CHECK(has(r, "\"total\":15"), "apiItems sort=%s: %.60s", s, r.c_str());
  }
  CHECK(has(apiItems("liked", "B (2)|C (3)", "score", 0, 50), "\"total\":"), "scoped query");
  std::string nx = apiNext("", "", "", 5);
  CHECK(has(nx, "\"items\":[{"), "apiNext returns cards");

  say("== Pixiv works (one card per multi-page work)");
  {
    const std::string h = "0123456789abcdef0123456789abcdef";
    put(L"P (5)\\100_p0 - t.jpg", 7001);
    put(L"P (5)\\100_p1 - t.jpg", 7002);
    put(L"P (5)\\100_p2 - t.jpg", 7003);
    put(L"P (5)\\200_p0 - u.png", 7004);
    put((L"P (5)\\300-" + widen(h) + L"_p0 - v.jpg").c_str(), 7005);
    put((L"P (5)\\300-" + widen(h) + L"_p1 - v.jpg").c_str(), 7006);
    put(L"P (5)\\cover.jpg", 7007);
    coreScanSync();
    CHECK(folder("P (5)").total == 7, "without grouping every image is a card: %d", folder("P (5)").total);
    setGroup(1);
    CHECK(folder("P (5)").total == 4 && folder("P (5)").images == 7, "grouped: %d cards, %d images",
          folder("P (5)").total, folder("P (5)").images);
    CHECK(rate("P (5)/100_p0 - t.jpg", 'L', "w1").status == 200, "rate a work");
    CHECK(item("P (5)/100_p1 - t.jpg").events == 1 && item("P (5)/100_p2 - t.jpg").events == 1,
          "one swipe rates every page of the work");
    CHECK(folder("P (5)").rated == 1 && folder("P (5)").liked == 1, "the work counts once for the folder");
    std::string cards = apiNext("P (5)", "", "", 10);
    CHECK(has(cards, "\"pages\":[{") && has(cards, "100_p2 - t.jpg"), "cards carry their page list");
    CHECK(rate("P (5)/100_p0 - t.jpg", 'D', "w2", "w1").status == 200, "correct a work rating");
    DebugItem p2 = item("P (5)/100_p2 - t.jpg");
    CHECK(p2.events == 1 && p2.last == 'D', "the correction replaced the rating on every page");
    CHECK(rate("P (5)/300-" + h + "_p1 - v.jpg", 'S', "w3").status == 200, "rate a work by any of its pages");
    CHECK(item("P (5)/300-" + h + "_p0 - v.jpg").last == 'S', "hash-style names group too");
    restart();
    CHECK(folder("P (5)").total == 4 && item("P (5)/100_p1 - t.jpg").last == 'D', "grouping survives a restart");
    setGroup(0);
    CHECK(folder("P (5)").total == 7 && folder("P (5)").rated == 5, "ungrouped again: %d cards, %d rated",
          folder("P (5)").total, folder("P (5)").rated);

    // a correction made after regrouping re-rates exactly the files the old rating covered
    setGroup(1);
    CHECK(rate("P (5)/100_p0 - t.jpg", 'L', "w4").status == 200, "rate the work again");
    setGroup(0);
    CHECK(rate("P (5)/100_p0 - t.jpg", 'S', "w5", "w4").status == 200, "correct it while ungrouped");
    CHECK(item("P (5)/100_p2 - t.jpg").last == 'S' && item("P (5)/100_p1 - t.jpg").last == 'S',
          "every page of the old rating got the correction");

    // one rating of a work, its pages corrected one by one once ungrouped: the rating's id still
    // leads to every page's correction (also after a restart), so cancelling it withdraws them all
    const int e0 = item("P (5)/100_p0 - t.jpg").events, e1 = item("P (5)/100_p1 - t.jpg").events,
              e2 = item("P (5)/100_p2 - t.jpg").events;
    setGroup(1);
    CHECK(rate("P (5)/100_p0 - t.jpg", 'L', "w6").status == 200, "a work rated once more");
    setGroup(0);
    CHECK(rate("P (5)/100_p0 - t.jpg", 'S', "w6a", "latest").status == 200 &&
              rate("P (5)/100_p1 - t.jpg", 'D', "w6b", "latest").status == 200,
          "two of its pages corrected one by one");
    restart();
    int st = 200;
    CHECK(has(apiRevoke("w6", st), "\"revoked\":3"), "cancelling the work's rating finds every page's rating now");
    CHECK(item("P (5)/100_p0 - t.jpg").events == e0 && item("P (5)/100_p1 - t.jpg").events == e1 &&
              item("P (5)/100_p2 - t.jpg").events == e2,
          "and withdraws them all: %d/%d/%d events (want %d/%d/%d)", item("P (5)/100_p0 - t.jpg").events,
          item("P (5)/100_p1 - t.jpg").events, item("P (5)/100_p2 - t.jpg").events, e0, e1, e2);
    // the sub gallery lists a picked work as one card with its pages, and the card's own fingerprint
    // (the cover) adds and removes the whole work, also when its pages were picked while ungrouped
    st = 200;
    const std::string w0 = item("P (5)/100_p0 - t.jpg").fp, w1 = item("P (5)/100_p1 - t.jpg").fp;
    setGroup(0);
    apiPick(w0, true, st);
    apiPick(w1, true, st);
    setGroup(1);
    const std::string pk = apiPicks();
    size_t withPages = 0;
    for (size_t at = pk.find("\"pages\":["); at != std::string::npos; at = pk.find("\"pages\":[", at + 1)) ++withPages;
    CHECK(st == 200 && withPages == 1 && has(pk, "100_p2 - t.jpg"), "a picked work comes with its pages, once: %.120s",
          pk.c_str());
    apiPick(w0, false, st);
    CHECK(!has(apiPicks(), "100_p"), "taking a work out removes every saved page of it");
    setGroup(0);
    apiPick(w1, true, st);  // only a later page saved
    setGroup(1);
    CHECK(has(apiPicks(), "\"name\":\"100_p0 - t.jpg\""), "a work saved by a later page is listed by its cover");
    CHECK(has(apiPick(w0, true, st), "\"count\":1"), "adding its cover adds nothing more");
    apiPick(w0, false, st);
    CHECK(!has(apiPicks(), "100_p"), "and the cover takes it out");
  }

  say("== archive (never show again)");
  {
    setGroup(1);
    int before = folder("P (5)").total;
    CHECK(rate("P (5)/100_p0 - t.jpg", 'A', "z1").status == 200, "archive a work");
    CHECK(item("P (5)/100_p2 - t.jpg").archived, "every page is archived");
    CHECK(folder("P (5)").total == before - 1, "the work leaves the folder's cards: %d -> %d", before,
          folder("P (5)").total);
    std::string cards = apiNext("P (5)", "", "", 20);
    CHECK(!has(cards, "100_p0 - t.jpg") && !has(cards, "100_p1 - t.jpg"), "archived cards are never offered");
    CHECK(!has(apiItems("all", "P (5)", "name", 0, 50), "100_p0"), "archived cards are not in the gallery");
    std::string arch = apiItems("archived", "P (5)", "name", 0, 50);
    CHECK(has(arch, "\"total\":1") && has(arch, "100_p2 - t.jpg"), "the archive view lists the work with its pages");
    restart();
    CHECK(item("P (5)/100_p1 - t.jpg").archived && folder("P (5)").total == before - 1, "archive survives a restart");
    CHECK(rate("P (5)/100_p1 - t.jpg", 'R', "z2").status == 200, "restore from the archive");
    CHECK(!item("P (5)/100_p0 - t.jpg").archived && !item("P (5)/100_p2 - t.jpg").archived,
          "restoring brings back every page");
    CHECK(folder("P (5)").total == before && item("P (5)/100_p0 - t.jpg").last == 'S', "and the old rating with it");
    // archiving by mistake and correcting it from history
    CHECK(rate("P (5)/200_p0 - u.png", 'A', "z3").status == 200 && item("P (5)/200_p0 - u.png").archived, "archive");
    CHECK(rate("P (5)/200_p0 - u.png", 'L', "z4", "z3").status == 200, "correct the archive");
    CHECK(!item("P (5)/200_p0 - u.png").archived && item("P (5)/200_p0 - u.png").last == 'L',
          "the correction un-archives and rates");
    setGroup(0);
  }

  say("== curated swipes, review share extremes, folder cards");
  {
    std::string sup = apiNext("", "", "", 50, "super");
    CHECK(has(sup, "\"kind\":\"curate\"") && !has(sup, "\"last\":\"L\"") && !has(sup, "\"last\":\"D\""),
          "curating super likes offers only super likes");
    std::string fresh = apiNext("", "", "", 50, "fresh");
    CHECK(!has(fresh, "\"rated\":true"), "fresh-only offers only unrated cards");
    coreSetPolicy(1.0, 1.0, 300, 0.35, 3.0);
    CHECK(!has(apiNext("", "", "", 50), "\"kind\":\"new\""), "reviews-only never offers new images");
    coreSetPolicy(0.0, 0.0, 300, 0.35, 3.0);
    CHECK(!has(apiNext("", "", "", 3), "\"kind\":\"review\""), "no reviews while new images remain");
    coreSetPolicy(0.35, 0.35, 300, 0.35, 3.0);
    std::string fc = apiFolderCards("liked", "", "rank");
    CHECK(has(fc, "\"folders\":[{") && has(fc, "\"cover\":{\"id\":"), "folder cards with covers");
    std::string a1 = apiItems("all", "", "random", 0, 5, 1), a2 = apiItems("all", "", "random", 0, 5, 2);
    CHECK(a1 != a2, "a new shuffle seed gives a new order");
  }

  say("== journal write rollback");
  {
    std::wstring journal = data + L"\\ratings.log";
    std::string full;
    readFileBytes(journal, full);
    appendFileBytes(journal, "12345\t1\tdeadbeef");  // torn fragment of an unacknowledged write
    restart();
    std::string after;
    readFileBytes(journal, after);
    CHECK(after == full, "a torn last line is cut off at startup (%zu -> %zu bytes)", full.size(), after.size());
  }

  say("== cancel one rating; reset a folder's ratings");
  {
    uint32_t s2 = 900;
    auto put2 = [&](const wchar_t* rel, size_t size) { writeRandom(lib + L"\\" + rel, size, s2++); };
    put2(L"R (7)\\r1.jpg", 9300);
    put2(L"R (7)\\r2.jpg", 9400);
    put2(L"R (7)\\sub\\r3.jpg", 9500);
    put2(L"Q (8)\\q1.jpg", 9600);
    restart();
    int st = 200;
    CHECK(rate("R (7)/r1.jpg", 'L', "rv1").status == 200 && item("R (7)/r1.jpg").rated, "a like before the cancel");
    CHECK(has(apiRevoke("rv1", st), "\"revoked\":1") && st == 200 && !item("R (7)/r1.jpg").rated &&
              item("R (7)/r1.jpg").events == 0,
          "cancel: the like is withdrawn, the image is unrated again");
    CHECK(has(apiRevoke("rv1", st), "\"revoked\":0"), "cancelling twice does nothing");
    restart();
    CHECK(!item("R (7)/r1.jpg").rated, "the cancel is kept in the journal");

    rate("R (7)/r1.jpg", 'L', "rv2");
    rate("R (7)/r2.jpg", 'S', "rv3");
    rate("R (7)/r2.jpg", 'A', "rv4");  // archived after its super like
    rate("R (7)/sub/r3.jpg", 'D', "rv5");
    rate("Q (8)/q1.jpg", 'L', "rv6");
    const double qScore = folder("Q (8)").score;
    std::string dry = apiResetRatings("R (7)", true, st);
    CHECK(has(dry, "\"images\":3") && has(dry, "\"ratings\":3") && item("R (7)/r1.jpg").rated,
          "reset, counted first: 3 images, nothing changed yet (%s)", dry.c_str());
    CHECK(has(apiResetRatings("", false, st), "error") && st == 400 && item("Q (8)/q1.jpg").rated,
          "a reset always names a folder");
    st = 200;
    std::string done = apiResetRatings("R (7)", false, st);
    CHECK(has(done, "\"reset\":true") && !item("R (7)/r1.jpg").rated && !item("R (7)/sub/r3.jpg").rated &&
              item("R (7)/r2.jpg").archived && item("R (7)/r2.jpg").nS == 0,
          "reset: the folder's images (subfolders too) are unrated; archived stays archived");
    CHECK(item("Q (8)/q1.jpg").rated && folder("Q (8)").score == qScore, "other folders keep their ratings");
    restart();
    CHECK(!item("R (7)/r1.jpg").rated && !item("R (7)/sub/r3.jpg").rated && item("R (7)/r2.jpg").archived &&
              item("Q (8)/q1.jpg").rated,
          "the reset is kept in the journal");
    rmTree(lib + L"\\R (7)");
    rmTree(lib + L"\\Q (8)");
    restart();
  }

  say("== a rating corrected elsewhere, then corrected or cancelled by its own id");
  {
    // the swipe screen keeps its rating's id; the gallery's full view corrects that rating with
    // "latest" meanwhile; a later correction (or cancel) by the swipe's id must act on the rating
    // that replaced it, never leave two
    uint32_t s3 = 1300;
    auto put3 = [&](const wchar_t* rel, size_t size) { writeRandom(lib + L"\\" + rel, size, s3++); };
    put3(L"T (10)\\t1.jpg", 9700);
    put3(L"T (10)\\t2.jpg", 9800);
    restart();
    auto ratings = [](const char* rel) {
      const std::string h = apiHistory(item(rel).id, "");
      int n = 0;
      for (size_t at = h.find("\"a\":"); at != std::string::npos; at = h.find("\"a\":", at + 1)) n++;
      return n;
    };
    CHECK(rate("T (10)/t1.jpg", 'L', "sw1").status == 200, "a swipe's like");
    CHECK(rate("T (10)/t1.jpg", 'S', "fv1", "latest").status == 200, "the full view corrects it to super");
    CHECK(rate("T (10)/t1.jpg", 'D', "sw2", "sw1").status == 200, "the swipe screen corrects its own rating");
    DebugItem t1 = item("T (10)/t1.jpg");
    CHECK(ratings("T (10)/t1.jpg") == 1 && t1.last == 'D', "one rating, the swipe's correction: %d ratings, last %c",
          ratings("T (10)/t1.jpg"), t1.last ? t1.last : '-');
    restart();
    t1 = item("T (10)/t1.jpg");
    CHECK(ratings("T (10)/t1.jpg") == 1 && t1.last == 'D', "the same after a restart: %d ratings, last %c",
          ratings("T (10)/t1.jpg"), t1.last ? t1.last : '-');
    int st = 200;
    CHECK(has(apiRevoke("sw1", st), "\"revoked\":1") && ratings("T (10)/t1.jpg") == 0 && !item("T (10)/t1.jpg").rated,
          "cancelling the swipe by its first id withdraws the rating that stands in for it");
    // across a restart in between: corrected elsewhere, restart, then corrected by the old id
    rate("T (10)/t2.jpg", 'L', "sw3");
    rate("T (10)/t2.jpg", 'S', "fv2", "latest");
    restart();
    CHECK(rate("T (10)/t2.jpg", 'D', "sw4", "sw3").status == 200 && ratings("T (10)/t2.jpg") == 1 &&
              item("T (10)/t2.jpg").last == 'D',
          "after a restart the old id still leads to the correction: %d ratings", ratings("T (10)/t2.jpg"));
    // a cancel's tick is never used again after a restart, so the cancelled rating's id cannot lead
    // to a fresh rating; and an old journal where that happened (v0.12.0) does not link them either
    rate("T (10)/t2.jpg", 'L', "sw8");
    apiRevoke("sw8", st);
    restart();
    rate("T (10)/t2.jpg", 'S', "sw9");
    CHECK(has(apiRevoke("sw8", st), "\"revoked\":0") && item("T (10)/t2.jpg").last == 'S',
          "cancelling a cancelled rating again leaves the fresh one after a restart");
    {
      DebugItem t2 = item("T (10)/t2.jpg");
      const std::string n = std::to_string(coreDebugTick() + 10), m = std::to_string(coreDebugTick() + 11);
      // v0.12.0 after a restart: a cancel (X at m) and then a fresh rating that reused tick m, later
      appendFileBytes(journal, n + "\t100\t" + t2.fp + "\tL\tx\tcid=old1\n" + m + "\t200\t" + t2.fp +
                                   "\tX\tx\ttarget=" + n + "\n" + m + "\t300\t" + t2.fp + "\tL\tx\tcid=fresh1\n");
      restart();
      const int before = item("T (10)/t2.jpg").events;
      CHECK(has(apiRevoke("old1", st), "\"revoked\":0") && item("T (10)/t2.jpg").events == before,
            "a reused tick in an old journal is not taken for a correction");
    }
    // a cancelled rating stays cancelled: its id does not lead anywhere else
    rate("T (10)/t1.jpg", 'L', "sw5");
    apiRevoke("sw5", st);
    rate("T (10)/t1.jpg", 'S', "sw6");
    CHECK(rate("T (10)/t1.jpg", 'D', "sw7", "sw5").status == 200 && ratings("T (10)/t1.jpg") == 2,
          "correcting a cancelled rating adds the new one and leaves the others: %d ratings", ratings("T (10)/t1.jpg"));
    rmTree(lib + L"\\T (10)");
    restart();
  }

  say("== the sub gallery (picks)");
  {
    int st = 200;
    // the restart above rescans in the background, and "scanning":false can come before that scan has
    // even started: wait for the pictures themselves (up to 10 s). a6, not a1: a1 has an identical
    // copy (dup.jpg) and which of the two is indexed depends on the scan order
    for (int i = 0; i < 1000 && !(item("A (1)/a6.jpg").present && item("B (2)/b2.jpg").present); ++i) platSleepMs(10);
    const DebugItem a6 = item("A (1)/a6.jpg"), b2 = item("B (2)/b2.jpg");
    CHECK(a6.present && b2.present, "the pictures to pick are there");
    CHECK(has(apiPicks(), "\"items\":[]"), "the sub gallery starts empty");
    apiPick(a6.fp, true, st);
    apiPick(b2.fp, true, st);
    apiPick(a6.fp, true, st);  // twice: still once
    std::string list = apiPicks();
    CHECK(st == 200 && has(list, "b2.jpg") && has(list, "a6.jpg") && list.find("b2.jpg") < list.find("a6.jpg") &&
              list.find("a6.jpg") == list.rfind("a6.jpg"),
          "picks are listed newest first, each once");
    coreForgetPicksForTest();
    CHECK(has(apiPicks(), "a6.jpg"), "picks are kept in picks.tsv");
    apiPick(a6.fp, false, st);
    CHECK(!has(apiPicks(), "a6.jpg") && has(apiPicks(), "b2.jpg"), "a pick can be taken out");
    st = 200;
    CHECK(has(apiPick("xyz", true, st), "error") && st == 400, "a bad fingerprint is refused");
    apiPick(b2.fp, false, st);
  }

  say("== settings: v0.4 files, Fast/Slow, advanced mode, keys of newer versions");
  {
    const Config saved = g_cfg;
    std::wstring cfgFile = data + L"\\config.txt";
    writeFileAtomic(cfgFile, "root=" + saved.root + "\nthrow_delay_ms=500\nlong_press_ms=300\nfuture_key=42\n");
    g_cfg = Config();
    loadConfig();
    CHECK(g_cfg.throwSlow && effectiveThrowMs() == 500 && effectiveHoldMs() == 400,
          "a v0.4 redirect time becomes Slow; simple mode uses 0.5 s and 0.4 s (%d, %d)", effectiveThrowMs(), effectiveHoldMs());
    g_cfg.advanced = true;
    CHECK(effectiveThrowMs() == 500 && effectiveHoldMs() == 300, "advanced mode uses the exact values");
    g_cfg.advanced = false;
    g_cfg.throwSlow = false;
    CHECK(effectiveThrowMs() == 0, "Fast commits at once");
    saveConfig();
    std::string written;
    readFileBytes(cfgFile, written);
    CHECK(has(written, "future_key=42") && has(written, "throw_mode=fast"), "unknown settings survive a save");
    ConfigPatch bad;
    std::string evil = "like:Arrow Right";  // a space (or '=' / newline) could break config.txt
    bad.customKeys = &evil;
    CHECK(!coreSetConfig(bad).empty() && g_cfg.customKeys.empty(), "malformed key bindings are refused");
    ConfigPatch good;
    std::string keys = "like:ArrowRight,KeyL;pass:ArrowLeft", mouse = "left:grab;right:back;wheelUp:like";
    good.customKeys = &keys;
    good.customMouse = &mouse;
    good.keyMode = 4;
    good.mouseMode = 3;
    CHECK(coreSetConfig(good).empty() && g_cfg.keyMode == 4 && g_cfg.mouseMode == 3 && g_cfg.customMouse == mouse,
          "custom layouts are stored");
    CHECK(has(apiState(), "\"customKeys\":\"like:ArrowRight,KeyL;pass:ArrowLeft\""), "and reach the page");

    // 0.7.4: 1 in 10 is the standard review share; 1 in 15 is stored exactly; V2 can hide its card buttons
    writeFileAtomic(cfgFile, "root=" + saved.root + "\n");
    g_cfg = Config();
    loadConfig();
    CHECK(std::abs(g_cfg.reviewShare - 0.1) < 1e-9 && g_cfg.swipeButtons, "without the keys: 1 in 10 reviews, card buttons shown");
    ConfigPatch share;
    share.reviewShare = 0.067;
    share.swipeButtons = 0;
    CHECK(coreSetConfig(share).empty(), "the review share and the button switch are accepted");
    g_cfg = Config();
    loadConfig();
    CHECK(std::abs(g_cfg.reviewShare - 0.067) < 1e-9 && !g_cfg.swipeButtons && has(apiState(), "\"swipeButtons\":false") &&
              has(apiState(), "\"reviewShare\":0.067"),
          "1 in 15 and hidden buttons survive a restart and reach the screens");
    g_cfg = saved;
    saveConfig();
  }

  say("== copies checked byte by byte; different images with one fingerprint kept apart");
  {
    restart();
    CHECK(coreDebugCopies() == 1 && item("A (1)/a1.jpg").present != item("A (1)/dup.jpg").present,
          "the identical pair is one image plus one recorded copy (%d)", coreDebugCopies());
    // Same size, same first and last 8 KB, different middle: one fingerprint, two images.
    mkdirs(lib + L"\\E (6)");
    const std::string head(8192, 'h'), tail(8192, 't');
    writeFileAtomic(lib + L"\\E (6)\\twin1.jpg", head + std::string(20000, '1') + tail);
    writeFileAtomic(lib + L"\\E (6)\\twin2.jpg", head + std::string(20000, '2') + tail);
    coreScanSync();
    DebugItem t1 = item("E (6)/twin1.jpg"), t2 = item("E (6)/twin2.jpg");
    CHECK(t1.present && t2.present && t1.fp != t2.fp, "both are images of their own");
    CHECK(rate("E (6)/twin1.jpg", 'L', "twin1-like").status == 200 && rate("E (6)/twin2.jpg", 'D', "twin2-pass").status == 200,
          "rate them differently");
    auto twinsKeepRatings = [&](const char* when) {
      DebugItem a = item("E (6)/twin1.jpg"), b = item("E (6)/twin2.jpg");
      CHECK(a.present && b.present && a.fp == t1.fp && b.fp == t2.fp && a.last == 'L' && b.last == 'D' && a.events == 1 &&
                b.events == 1,
            "%s: each keeps its own identity and rating", when);
    };
    restart();
    twinsKeepRatings("after a restart");
    DeleteFileW((data + L"\\index.tsv").c_str());
    DeleteFileW((data + L"\\index2.tsv").c_str());
    restart();
    twinsKeepRatings("after losing the index cache");

    // A copy carries the image on when the original is deleted.
    writeRandom(lib + L"\\E (6)\\orig.jpg", 30000, 991);
    CopyFileW((lib + L"\\E (6)\\orig.jpg").c_str(), (lib + L"\\E (6)\\orig-copy.jpg").c_str(), FALSE);
    coreScanSync();
    DebugItem o = item("E (6)/orig.jpg"), oc = item("E (6)/orig-copy.jpg");
    const bool origHolds = o.present;
    CHECK(o.present != oc.present, "one of the two identical files is the image");
    CHECK(rate(origHolds ? "E (6)/orig.jpg" : "E (6)/orig-copy.jpg", 'S', "orig-super").status == 200, "rate it");
    DeleteFileW((lib + (origHolds ? L"\\E (6)\\orig.jpg" : L"\\E (6)\\orig-copy.jpg")).c_str());
    coreScanSync();
    DebugItem left = item(origHolds ? "E (6)/orig-copy.jpg" : "E (6)/orig.jpg");
    CHECK(left.present && left.last == 'S' && left.events == 1, "the remaining copy becomes the image, with its rating");
    restart();
    CHECK(coreDebugHashed() == 0, "and nothing needs reading again after a restart (%d)", coreDebugHashed());
  }

  say("== index v2: migration from v1, several photo folders");
  {
    std::wstring idx1 = data + L"\\index.tsv", idx2 = data + L"\\index2.tsv";
    std::string v1, v2;
    restart();  // writes both files
    readFileBytes(idx1, v1);
    readFileBytes(idx2, v2);
    CHECK(v2.rfind("#choosen-index-2\t", 0) == 0 && has(v2, ("\n#root\t0\t" + g_cfg.root + "\n").c_str()),
          "v2 names its photo folders");
    CHECK(v1.rfind("#choosen-index-1\t" + g_cfg.root + "\t", 0) == 0, "v1 keeps the header older exes read");
    // Known copies are listed only in v2, so a migration from v1 reads just those again.
    const int total = folder("").total, dupes = coreDebugHashed(), knownCopies = coreDebugCopies();
    CHECK(dupes == 0 && knownCopies == 1, "a restart reads no file again, the copy included (%d hashed, %d copies)", dupes,
          knownCopies);
    const uint64_t t0 = coreDebugTick();
    const DebugItem b1 = item("B (2)/b1.png");
    DeleteFileW(idx2.c_str());
    restart();
    CHECK(coreDebugHashed() == knownCopies && coreDebugCopies() == knownCopies,
          "migrating a v1 index fingerprints no indexed image again, only the known copy (%d hashed, %d expected)",
          coreDebugHashed(), knownCopies);
    CHECK(folder("").total == total && coreDebugTick() == t0 && item("B (2)/b1.png").events == b1.events &&
              item("B (2)/b1.png").p == b1.p,
          "same library and ratings after the migration");
    CHECK(readFileBytes(idx2, v2) && v2.rfind("#choosen-index-2\t", 0) == 0, "the v2 file is written after migrating");
    DeleteFileW(idx1.c_str());
    restart();
    CHECK(coreDebugHashed() == dupes && folder("").total == total, "the v2 file alone is enough");

    std::wstring lib2 = base + L"\\lib2";
    mkdirs(lib2 + L"\\Z (9)");
    CopyFileW((lib + L"\\B (2)\\b3.jpg").c_str(), (lib2 + L"\\Z (9)\\copy.jpg").c_str(), FALSE);
    writeRandom(lib2 + L"\\Z (9)\\z1.jpg", 14000, 777);
    writeRandom(lib2 + L"\\Z (9)\\early.jpg", 13000, 778);
    g_cfg.moreRoots = {narrow(lib2)};
    restart();
    CHECK(item("Z (9)/z1.jpg").present && item("Z (9)/z1.jpg").root == 1, "images of a second photo folder are indexed");
    CHECK(item("B (2)/b3.jpg").present && item("B (2)/b3.jpg").root == 0 && !item("Z (9)/copy.jpg").present,
          "a copy in a later photo folder is the same image, kept in the first folder");
    CHECK(rate("Z (9)/early.jpg", 'L', "v2-early").status == 200, "rate an image of the second folder");
    // The same file now also appears in the first folder: it moves there, with its rating.
    CopyFileW((lib2 + L"\\Z (9)\\early.jpg").c_str(), (lib + L"\\A (1)\\late.jpg").c_str(), FALSE);
    for (int round = 0; round < 2; ++round) {
      restart();
      DebugItem late = item("A (1)/late.jpg");
      CHECK(late.present && late.root == 0 && late.events == 1 && !item("Z (9)/early.jpg").present,
            "round %d: the first photo folder always wins, and the rating follows the image", round);
    }
    readFileBytes(idx1, v1);
    readFileBytes(idx2, v2);
    CHECK(!has(v1, "z1.jpg") && has(v2, "\t1\tZ (9)/z1.jpg\n"), "v1 lists only the first photo folder, v2 all of them");

    // An older exe rewrites v1 (no stamp) after a file was renamed; v2 is now stale for folder 0.
    const int dupes2 = coreDebugHashed();
    const DebugItem a5 = item("A (1)/a5.jpg");
    CHECK(a5.present, "a5 is indexed");
    MoveFileW((lib + L"\\A (1)\\a5.jpg").c_str(), (lib + L"\\A (1)\\a5-old-exe.jpg").c_str());
    std::string old = "#choosen-index-1\t" + g_cfg.root + "\n" + v1.substr(v1.find('\n') + 1);
    size_t at = old.find("\tA (1)/a5.jpg\n");
    if (at != std::string::npos) old.replace(at, 14, "\tA (1)/a5-old-exe.jpg\n");
    writeFileAtomic(idx1, old);
    restart();
    CHECK(coreDebugHashed() == dupes2, "a v1 file rewritten by an older exe is used for the first folder (%d hashed, %d expected)",
          coreDebugHashed(), dupes2);
    CHECK(item("A (1)/a5-old-exe.jpg").present && item("A (1)/a5-old-exe.jpg").events == a5.events &&
              item("Z (9)/z1.jpg").present,
          "renamed file keeps its ratings; the second folder still comes from v2");

    g_cfg.moreRoots.clear();
    restart();
    CHECK(!item("Z (9)/z1.jpg").present && folder("").total == total + 1, "removing the second folder hides its images (%d)",
          folder("").total);
  }

  say("== settings: add and remove photo folders");
  {
    const Config saved = g_cfg;
    std::wstring cfgFile = data + L"\\config.txt";
    const int total = folder("").total;
    std::wstring lib3 = base + L"\\lib3";
    writeRandom(lib3 + L"\\Q (7)\\q1.jpg", 21000, 901);
    writeRandom(lib3 + L"\\Q (7)\\q2.jpg", 22000, 902);
    writeRandom(lib3 + L"\\A (1)\\qa.jpg", 23000, 903);  // same artist folder name as in the first photo folder
    writeRandom(lib3 + L"\\qtop.jpg", 24000, 904);       // directly in the second photo folder
    const std::string k3 = narrow(lib3);                  // folder key of the second photo folder
    auto setRoots = [&](const std::string* root, std::vector<std::string> more) {
      ConfigPatch c;
      c.root = root;
      c.moreRoots = &more;
      std::string err = coreSetConfig(c);
      coreScanSync();
      return err;
    };

    CHECK(setRoots(nullptr, {narrow(lib3)}).empty() && g_cfg.moreRoots.size() == 1, "a second photo folder is added");
    CHECK(item("Q (7)/q1.jpg").present && item("Q (7)/q1.jpg").root == 1 && folder(k3).total == 4 &&
              folder("").total == total,
          "its images are in the library, under their own top folder (%d / %d)", folder(k3).total, folder("").total);
    CHECK(has(apiState(), "\"moreRoots\":[\"") && has(apiItems("all", "", "name", 0, 500), "q1.jpg"),
          "the page sees the folder and its images");
    CHECK(rate("Q (7)/q1.jpg", 'S', "q1-super").status == 200, "rate an image of the added folder");

    say("== ranking and scopes across photo folders");
    {
      DebugFolder top = folder(k3), q = folder(k3 + "/Q (7)"), a3 = folder(k3 + "/A (1)"), a0 = folder("A (1)");
      CHECK(top.depth == 0 && q.depth == 1 && q.rated == 1 && q.rank >= 1,
            "a further photo folder is a top folder; its artists rank with the others (rank %d)", q.rank);
      CHECK(a3.total == 1 && a0.total > 1 && a3.depth == 1, "artist folders with the same name stay separate (%d / %d)",
            a3.total, a0.total);
      auto items = [&](const std::string& scope) { return apiItems("all", scope, "name", 0, 500); };
      std::string s;
      s = items(k3);
      CHECK(has(s, "q1.jpg") && has(s, "qa.jpg") && has(s, "qtop.jpg") && !has(s, "a2.jpg"), "scope = a whole further photo folder");
      s = items("/*");
      CHECK(has(s, "a2.jpg") && has(s, "top.jpg") && !has(s, "q1.jpg"), "scope /* = the whole first photo folder");
      s = items("A (1)");
      CHECK(has(s, "a2.jpg") && !has(s, "qa.jpg"), "a scope saved before the update still selects the same folder");
      s = items("/");
      CHECK(has(s, "top.jpg") && !has(s, "a2.jpg") && !has(s, "qtop.jpg"), "/ = only the images directly in the first photo folder");
      s = items(k3 + "/");
      CHECK(has(s, "qtop.jpg") && !has(s, "q1.jpg") && !has(s, "\"top.jpg\""), "<folder>/ = only the images directly in it");
      s = items("A (1)|" + k3 + "/Q (7)");
      CHECK(has(s, "a2.jpg") && has(s, "q1.jpg") && !has(s, "qa.jpg"), "scopes combine folders of both photo folders");
      CHECK(has(apiFolderCards("all", "", "rank"), "\"name\":\"Q (7)\""), "gallery folder cards include the second photo folder");
    }

    say("== per-folder option: include subfolders");
    {
      auto setDepths = [&](std::vector<int> depths) {
        ConfigPatch c;
        c.maxDepths = &depths;
        std::string err = coreSetConfig(c);
        coreScanSync();
        return err;
      };
      CHECK(folder("A (1)").artist && !folder("").artist && folder(k3 + "/Q (7)").artist && !folder(k3).artist,
            "by default every photo folder is read with its subfolders and artists sit at the artist depth");
      CHECK(coreHiddenBy(k3, 0) == 3 && coreHiddenBy(k3, -1) == 0 && coreHiddenBy(saved.root, 0) > 0,
            "before saving, the page can show how many images would be hidden (%d)", coreHiddenBy(k3, 0));
      CHECK(setDepths({-1, 0}).empty() && g_cfg.maxDepths.size() == 2 && g_cfg.maxDepths[1] == 0, "subfolders off for the second folder");
      CHECK(item("qtop.jpg").present && !item("Q (7)/q1.jpg").present && !item("A (1)/qa.jpg").present && folder(k3).total == 1,
            "only the images directly in it are read (%d)", folder(k3).total);
      CHECK(item("A (1)/a2.jpg").present && folder("A (1)").total > 1, "the other photo folder is not affected");
      CHECK(rate("qtop.jpg", 'L', "qtop-like").status == 200, "rate an image directly in the folder");
      DebugFolder one = folder(k3), a = folder("B (2)");
      CHECK(one.artist && one.rank >= 1 && a.artist && one.rank != a.rank,
            "the folder is one artist, ranked together with the artist folders (rank %d vs %d)", one.rank, a.rank);
      std::string cfgNow;
      readFileBytes(cfgFile, cfgNow);
      CHECK(has(cfgNow, "\nroot2_max_depth=0\n") && !has(cfgNow, "\nroot_max_depth="), "stored as root2_max_depth=0");
      const Config keep = g_cfg;
      g_cfg = Config();
      loadConfig();
      CHECK(g_cfg.maxDepths.size() == 2 && g_cfg.maxDepths[0] == -1 && g_cfg.maxDepths[1] == 0, "and read back after a restart");
      g_cfg = keep;
      CHECK(!setDepths({-1, 99}).empty() && g_cfg.maxDepths[1] == 0, "an out-of-range value is refused");
      CHECK(setDepths({-1, -1}).empty(), "subfolders on again");
      DebugItem q1 = item("Q (7)/q1.jpg");
      CHECK(q1.present && q1.events == 1 && q1.last == 'S' && folder(k3).total == 4 && folder(k3 + "/Q (7)").artist,
            "hidden images come back with their ratings (events %d)", q1.events);

      say("== advanced mode: maximum subfolder depth per photo folder");
      writeRandom(lib3 + L"\\Q (7)\\deep\\q3.jpg", 25000, 905);  // two levels below the photo folder
      coreScanSync();
      CHECK(item("Q (7)/deep/q3.jpg").present && coreHiddenBy(k3, 1) == 1, "a limit of 1 would hide the deeper image");
      CHECK(rate("Q (7)/deep/q3.jpg", 'L', "q3-like").status == 200, "rate the deeper image");
      CHECK(setDepths({-1, 1}).empty() && g_cfg.maxDepths[1] == 1, "a maximum depth of 1 is stored");
      CHECK(!item("Q (7)/deep/q3.jpg").present && item("Q (7)/q1.jpg").present && item("qtop.jpg").present,
            "images deeper than the limit are not read; the levels above are");
      readFileBytes(cfgFile, cfgNow);
      CHECK(has(cfgNow, "\nroot2_max_depth=1\n"), "stored as root2_max_depth=1");
      ConfigPatch deeper;
      deeper.artistDepth = 2;
      CHECK(coreSetConfig(deeper).empty() && g_cfg.artistDepth == 2, "artist depth 2");
      CHECK(folder(k3 + "/Q (7)").artist && !folder("A (1)").artist,
            "the artist depth is clamped to a folder's limit: level 1 there, level 2 elsewhere");
      ConfigPatch back;
      back.artistDepth = 1;
      coreSetConfig(back);
      CHECK(!setDepths({-1, kMaxSubfolderDepth + 1}).empty() && !setDepths({-2, -1}).empty() &&
                setDepths({kMaxSubfolderDepth, -1}).empty(),
            "the server accepts exactly -1..%d", kMaxSubfolderDepth);
      CHECK(has(apiState(), ("\"maxSubfolderDepth\":" + std::to_string(kMaxSubfolderDepth)).c_str()),
            "and tells the page the same range");
      DebugItem q3 = item("Q (7)/deep/q3.jpg");
      CHECK(q3.present && q3.events == 1, "raising the limit brings the deeper image back with its rating");
      CHECK(setDepths({-1, -1}).empty(), "no limit again");
    }

    say("== a photo folder without subfolders is ranked as one folder");
    {
      std::wstring flat = base + L"\\flat";
      writeRandom(flat + L"\\f1.jpg", 26000, 931);
      writeRandom(flat + L"\\f2.jpg", 27000, 932);
      const std::string kf = narrow(flat);
      CHECK(setRoots(nullptr, {narrow(lib3), kf}).empty(), "add a folder whose images are all directly in it");
      CHECK(rate("f1.jpg", 'L', "flat-like").status == 200, "rate one of its images");
      DebugFolder f = folder(kf), q = folder(k3 + "/Q (7)");
      CHECK(f.artist && f.rank >= 1 && q.artist && f.rank != q.rank,
            "it is one row of the ranking, next to the folders of the other photo folders (rank %d)", f.rank);
      CHECK(!folder(k3).artist, "a photo folder with subfolders still ranks those subfolders");
      CHECK(setRoots(nullptr, {narrow(lib3)}).empty(), "remove it again");
    }
    std::string written;
    readFileBytes(cfgFile, written);
    CHECK(has(written, ("\nroot=" + saved.root + "\nroot2=" + narrow(lib3) + "\n").c_str()),
          "config.txt stores it as root2= right after root=");
    g_cfg = Config();
    loadConfig();
    CHECK(g_cfg.moreRoots.size() == 1 && g_cfg.moreRoots[0] == narrow(lib3) && g_cfg.root == saved.root,
          "the list of photo folders survives a restart");

    std::string missing = narrow(base + L"\\no-such-folder");
    CHECK(!setRoots(nullptr, {narrow(lib3), missing}).empty() && g_cfg.moreRoots.size() == 1,
          "a folder that does not exist is refused and nothing is saved");

    CHECK(setRoots(nullptr, {}).empty() && g_cfg.moreRoots.empty(), "the folder is removed");
    CHECK(!item("Q (7)/q1.jpg").present && folder("").total == total, "its images leave the library (total %d)",
          folder("").total);
    std::string journalText;
    CHECK(readFileBytes(data + L"\\ratings.log", journalText) && has(journalText, "q1-super"),
          "its ratings stay in the rating log");
    CHECK(setRoots(nullptr, {narrow(lib3)}).empty(), "the folder is added again");
    DebugItem q1 = item("Q (7)/q1.jpg");
    CHECK(q1.present && q1.events == 1 && q1.last == 'S' && q1.p == 86,
          "its images come back with their ratings (events %d, points %.0f)", q1.events, q1.p);

    std::string empty;
    CHECK(setRoots(&empty, {narrow(lib3)}).empty() && g_cfg.root == narrow(lib3) && g_cfg.moreRoots.empty(),
          "an emptied first folder is replaced by the next one");
    CHECK(item("Q (7)/q1.jpg").present && item("Q (7)/q1.jpg").root == 0 && item("Q (7)/q1.jpg").events == 1,
          "images keep their ratings when their folder moves up");
    std::string first = saved.root;
    CHECK(setRoots(&first, {}).empty() && g_cfg.root == saved.root && folder("").total == total,
          "back to the single original folder (total %d)", folder("").total);
    readFileBytes(cfgFile, written);
    CHECK(!has(written, "root2="), "no root2= line once there is one folder");

    // Overlapping photo folders are refused, and a refusal saves nothing.
    std::string cfgBefore;
    readFileBytes(cfgFile, cfgBefore);
    const std::string r0 = saved.root;
    auto refused = [&](std::vector<std::string> more, const char* mustName) {
      std::string err = setRoots(nullptr, more);
      std::string cfgNow;
      readFileBytes(cfgFile, cfgNow);
      return !err.empty() && has(err, mustName) && g_cfg.moreRoots.empty() && cfgNow == cfgBefore;
    };
    std::string upper = r0;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    CHECK(refused({r0}, r0.c_str()), "the same folder twice is refused");
    CHECK(refused({upper + "\\"}, "同じフォルダ"), "... also in other letter case and with a trailing backslash");
    CHECK(refused({narrow(lib3) + "\\..\\lib"}, "同じフォルダ"), "... also written with \"..\"");
    CHECK(refused({r0 + "\\A (1)"}, r0.c_str()), "a folder inside a photo folder is refused, naming that photo folder");
    CHECK(refused({narrow(base)}, r0.c_str()), "a folder that contains a photo folder is refused, naming that photo folder");
    CHECK(refused({narrow(lib3), narrow(lib3) + "\\Q (7)"}, "Q (7)"), "overlaps among the further folders are refused too");
    mkdirs(base + L"\\lib-other");
    CHECK(setRoots(nullptr, {narrow(base + L"\\lib-other")}).empty() && g_cfg.moreRoots.size() == 1,
          "a sibling whose name only starts the same (lib / lib-other) is allowed");
    CHECK(setRoots(nullptr, {}).empty(), "and removed again");
    g_cfg = saved;
    saveConfig();
  }

  say("== export (書き出し): copy or move one kind into a new folder");
  {
    // A library and data folder of its own, so the sections above and below are not affected.
    const Config saved = g_cfg;
    const std::wstring savedData = g_dataDir;
    const std::wstring xlib = base + L"\\xlib", xdata = base + L"\\xdata", out = base + L"\\xout";
    mkdirs(xdata);
    mkdirs(out);
    g_dataDir = xdata;
    g_cfg = Config{};
    g_cfg.root = narrow(xlib);
    writeRandom(xlib + L"\\K (1)\\k1.jpg", 12000, 501);
    writeRandom(xlib + L"\\K (1)\\sub\\k2.jpg", 12500, 502);
    writeRandom(xlib + L"\\K (1)\\k3.jpg", 13000, 503);
    writeRandom(xlib + L"\\L (2)\\l1.jpg", 13500, 504);
    writeRandom(xlib + L"\\L (2)\\l2.jpg", 14000, 505);
    CopyFileW((xlib + L"\\K (1)\\k3.jpg").c_str(), (xlib + L"\\L (2)\\k3-copy.jpg").c_str(), FALSE);
    restart();
    const bool k3Holds = item("K (1)/k3.jpg").present;  // which of the two identical files is the image
    const std::string k3 = k3Holds ? "K (1)/k3.jpg" : "L (2)/k3-copy.jpg";
    CHECK(rate("K (1)/k1.jpg", 'L', "x1").status == 200 && rate("K (1)/sub/k2.jpg", 'S', "x2").status == 200 &&
              rate("L (2)/l1.jpg", 'D', "x3").status == 200 && rate(k3, 'A', "x4").status == 200,
          "rate: like, super like, pass, archive");
    CHECK(coreExportCount("liked", false) == 2 && coreExportCount("likeOnly", false) == 1 &&
              coreExportCount("super", false) == 1 && coreExportCount("passed", false) == 1 &&
              coreExportCount("archived", false) == 1 && coreExportCount("all", false) == 0,
          "counts per kind (liked %d, likeOnly %d, super %d, passed %d, archived %d)", coreExportCount("liked", false),
          coreExportCount("likeOnly", false), coreExportCount("super", false), coreExportCount("passed", false),
          coreExportCount("archived", false));
    CHECK(coreExportCount("archived", true) == 2, "a move also takes the byte-identical copy (%d)",
          coreExportCount("archived", true));

    auto waitExport = [] {
      for (int i = 0; i < 1000 && has(coreExportState(), "\"running\":true"); ++i) platSleepMs(10);
      return coreExportState();
    };
    int y, m, d;
    platLocalDate(nowMs(), y, m, d);
    char day[16];
    snprintf(day, sizeof day, "%04d-%02d-%02d", y, m, d);
    const std::wstring liked = out + L"\\Choosen 好評と超好評 " + widen(day);

    CHECK(coreExportStart("liked", narrow(out), false).empty(), "copying the liked images starts");
    std::string st = waitExport();
    CHECK(has(st, "\"running\":false") && has(st, "\"done\":2,") && has(st, "\"failed\":0,") &&
              has(st, "\"total\":2,") && has(st, jsonStr(narrow(liked)).c_str()),
          "the job reports its counts and its new folder: %s", st.c_str());
    CHECK(platFileSize(liked + L"\\K (1)\\k1.jpg") == 12000 &&
              platFileSize(liked + L"\\K (1)\\sub\\k2.jpg") == 12500 && platFileSize(liked + L"\\L (2)\\l1.jpg") < 0,
          "a copy keeps each file's relative path, and only the liked kind is there");
    CHECK(platFileSize(xlib + L"\\K (1)\\k1.jpg") == 12000 && item("K (1)/k1.jpg").present &&
              platFileSize(xlib + L"\\K (1)\\sub\\k2.jpg") == 12500,
          "copying leaves the originals in place");
    CHECK(coreExportStart("liked", narrow(out), false).empty() && has(waitExport(), "\"done\":2,") &&
              platFileSize(liked + L" (2)\\K (1)\\k1.jpg") == 12000,
          "the same export again gets a new folder of its own (\" (2)\")");

    std::string upper = narrow(xlib);
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    CHECK(!coreExportStart("liked", narrow(xlib) + "\\K (1)", false).empty() &&
              !coreExportStart("liked", upper + "\\", false).empty() &&
              !isDirectory(xlib + L"\\K (1)\\Choosen 好評と超好評 " + widen(day)) &&
              !isDirectory(xlib + L"\\Choosen 好評と超好評 " + widen(day)),
          "a destination inside (or equal to) a photo folder is refused, in any letter case");
    CHECK(!coreExportStart("all", narrow(out), false).empty() && !coreExportStart("liked", "", false).empty() &&
              !coreExportStart("liked", narrow(base + L"\\no-such-folder"), false).empty(),
          "an unknown kind, no destination and a missing destination are refused");
    {
      auto call = [](const char* method, const char* query) {
        ApiCall q;
        q.method = method;
        q.path = "/api/export";
        parseQueryInto(query, q.params);
        int status = 200;
        std::string body;
        apiRoute(q, status, body);
        return body;
      };
      CHECK(has(call("GET", "what=archived&mode=move"), "\"count\":2}") &&
                has(call("GET", "what=liked"), "\"count\":2}"),
            "GET /api/export with what= also tells the count");
      CHECK(has(call("POST", ("what=archived&mode=delete&to=" + narrow(out)).c_str()), "\"ok\":false") &&
                platFileSize(xlib + L"\\K (1)\\k3.jpg") == 13000 &&
                platFileSize(xlib + L"\\L (2)\\k3-copy.jpg") == 13000,
            "an unknown mode is refused (only mode=move moves files)");
    }

    const std::wstring arch = out + L"\\Choosen アーカイブ " + widen(day);
    CHECK(coreExportStart("liked", narrow(out), true) == "移動できるのはアーカイブと見送りだけです" &&
              coreExportStart("super", narrow(out), true) == "移動できるのはアーカイブと見送りだけです",
          "favourites can be copied but not moved (the ranking would change)");
    CHECK(coreExportStart("archived", narrow(out), true).empty(), "moving the archived images starts");
    st = waitExport();
    CHECK(has(st, "\"done\":2,") && has(st, "\"failed\":0,") && has(st, "\"move\":true"), "both files moved: %s",
          st.c_str());
    CHECK(platFileSize(xlib + L"\\K (1)\\k3.jpg") < 0 && platFileSize(xlib + L"\\L (2)\\k3-copy.jpg") < 0,
          "a move removes the originals, the identical copy included");
    CHECK(platFileSize(arch + L"\\K (1)\\k3.jpg") == 13000 && platFileSize(arch + L"\\L (2)\\k3-copy.jpg") == 13000,
          "and they arrive in the new folder under their relative paths");
    bool gone = false;
    for (int i = 0; i < 1000 && !gone; ++i) {
      gone = !item(k3).present;
      if (!gone) platSleepMs(10);
    }
    for (int i = 0; i < 1000 && !has(apiState(), "\"scanning\":false"); ++i) platSleepMs(10);
    DebugItem k3Now = item(k3);
    CHECK(gone && k3Now.archived && k3Now.events == 1 && item("K (1)/k1.jpg").present,
          "the job rescans: moved files leave the library, their ratings stay");
    CHECK(coreExportStart("archived", narrow(out), true) == "該当する画像がありません", "nothing left to move is refused");

    // Never overwrite: a name that is taken gets a number instead.
    const std::wstring src = base + L"\\xsrc", dst = out + L"\\taken";
    writeRandom(src + L"\\a.jpg", 3000, 601);
    writeRandom(dst + L"\\a.jpg", 4000, 602);
    CHECK(!platCopyNoReplace(src + L"\\a.jpg", dst + L"\\a.jpg") &&
              !platMoveNoReplace(src + L"\\a.jpg", dst + L"\\a.jpg") && platFileSize(dst + L"\\a.jpg") == 4000 &&
              platFileSize(src + L"\\a.jpg") == 3000,
          "copy and move refuse an existing target and change neither file");
    const std::string to = narrow(dst + L"\\a.jpg");
    const std::string first = coreDebugExportPlace(narrow(src + L"\\a.jpg"), to, false);
    const std::string second = coreDebugExportPlace(narrow(src + L"\\a.jpg"), to, true);
    CHECK(first == narrow(dst + L"\\a (2).jpg") && second == narrow(dst + L"\\a (3).jpg") &&
              platFileSize(dst + L"\\a.jpg") == 4000 && platFileSize(dst + L"\\a (2).jpg") == 3000 &&
              platFileSize(dst + L"\\a (3).jpg") == 3000 && platFileSize(src + L"\\a.jpg") < 0,
          "an existing name is kept and the file is numbered instead (%s, %s)", first.c_str(), second.c_str());

    g_cfg = saved;
    g_dataDir = savedData;
    restart();
  }

  say("== sorting into subfolders by verdict (評価ごとにサブフォルダへ), and undoing it");
  {
    const Config saved = g_cfg;
    const std::wstring savedData = g_dataDir;
    const std::wstring slib = base + L"\\slib", sdata = base + L"\\sdata";
    mkdirs(sdata);
    g_dataDir = sdata;
    g_cfg = Config{};
    g_cfg.root = narrow(slib);
    writeRandom(slib + L"\\Pixiv\\p1.jpg", 11000, 701);
    writeRandom(slib + L"\\Pixiv\\p2.jpg", 11500, 702);
    writeRandom(slib + L"\\Pixiv\\p3.jpg", 12000, 703);
    writeRandom(slib + L"\\Pixiv\\p4.jpg", 12500, 704);
    writeRandom(slib + L"\\Other\\o1.jpg", 13000, 705);
    restart();
    CHECK(rate("Pixiv/p1.jpg", 'D', "s1").status == 200 && rate("Pixiv/p2.jpg", 'L', "s2").status == 200 &&
              rate("Pixiv/p3.jpg", 'S', "s3").status == 200 && rate("Other/o1.jpg", 'D', "s4").status == 200,
          "rate: pass, like, super like in Pixiv; pass in Other");
    auto waitJob = [] {
      for (int i = 0; i < 1000 && has(coreExportState(), "\"running\":true"); ++i) platSleepMs(10);
      for (int i = 0; i < 1000 && !has(apiState(), "\"scanning\":false"); ++i) platSleepMs(10);
      return coreExportState();
    };
    CHECK(has(coreSortCount("Pixiv", "", "passed,likeOnly,super"), "{\"passed\":1,\"likeOnly\":1,\"super\":1,\"undo\":0}"),
          "counts per kind in the folder: %s", coreSortCount("Pixiv", "", "passed,likeOnly,super").c_str());
    CHECK(!coreSortStart("", "", "passed").empty() && !coreSortStart("Pixiv", "", "archived").empty(),
          "no folder and no pictures, or an unknown kind, is refused (never the whole library)");
    CHECK(coreSortStart("Pixiv", "", "passed,likeOnly,super").empty(), "sorting the Pixiv folder starts");
    std::string st = waitJob();
    CHECK(has(st, "\"done\":3,") && has(st, "\"failed\":0,") && has(st, "\"what\":\"sort\""), "three files moved: %s",
          st.c_str());
    CHECK(platFileSize(slib + L"\\Pixiv\\見送り\\p1.jpg") == 11000 && platFileSize(slib + L"\\Pixiv\\好評\\p2.jpg") == 11500 &&
              platFileSize(slib + L"\\Pixiv\\超好評\\p3.jpg") == 12000 && platFileSize(slib + L"\\Pixiv\\p1.jpg") < 0,
          "each picture is in its kind's folder next to where it was");
    CHECK(platFileSize(slib + L"\\Pixiv\\p4.jpg") == 12500 && platFileSize(slib + L"\\Other\\o1.jpg") == 13000,
          "an unrated picture and the other folder stay where they were");
    DebugItem p1 = item("Pixiv/見送り/p1.jpg"), p3 = item("Pixiv/超好評/p3.jpg");
    CHECK(p1.present && p1.last == 'D' && p3.present && p3.last == 'S',
          "after the rescan the moved pictures keep their ratings (they follow the content)");
    CHECK(has(coreSortCount("Pixiv", "", "passed,likeOnly,super"), "{\"passed\":0,\"likeOnly\":0,\"super\":0,\"undo\":3}"),
          "sorted pictures are not sorted again, and three moves can be undone");
    CHECK(has(coreSortMoves(), "p1.jpg") && has(coreSortMoves(), "見送り"), "the moves are listed (for the phone's gallery)");

    CHECK(coreSortUndo().empty(), "undo starts");
    st = waitJob();
    CHECK(has(st, "\"done\":3,") && platFileSize(slib + L"\\Pixiv\\p1.jpg") == 11000 &&
              platFileSize(slib + L"\\Pixiv\\p3.jpg") == 12000 && platFileSize(slib + L"\\Pixiv\\見送り\\p1.jpg") < 0,
          "undo moves them back: %s", st.c_str());
    CHECK(item("Pixiv/p1.jpg").present && item("Pixiv/p1.jpg").last == 'D' && !coreSortUndo().empty(),
          "back in place with their ratings; nothing more to undo");
    CHECK(!isDirectory(slib + L"\\Pixiv\\見送り") && !isDirectory(slib + L"\\Pixiv\\超好評") && isDirectory(slib + L"\\Pixiv"),
          "the emptied kind folders are removed");

    const int o1 = item("Other/o1.jpg").id;
    CHECK(has(coreSortCount("", std::to_string(o1), "passed"), "\"passed\":1,") &&
              coreSortStart("", std::to_string(o1), "passed").empty(),
          "chosen pictures (ids) can be sorted too");
    waitJob();
    CHECK(platFileSize(slib + L"\\Other\\見送り\\o1.jpg") == 13000 && platFileSize(slib + L"\\Pixiv\\p1.jpg") == 11000,
          "only the chosen picture moved");

    g_cfg = saved;
    g_dataDir = savedData;
    restart();
  }

  say("== phones: request checks, pairing and tokens");
  {
    auto req = [](const char* method, const char* path, std::vector<std::pair<std::string, std::string>> h) {
      HttpRequest q;
      q.method = method;
      q.path = path;
      for (auto& kv : h) q.headers[kv.first] = kv.second;
      return q;
    };
    const int port = 47801;
    CHECK(remoteCheckRequest(req("GET", "/api/state", {{"host", "192.168.1.20:47801"}}), port).empty(), "a LAN address is accepted");
    CHECK(!remoteCheckRequest(req("GET", "/", {{"host", "evil.example:47801"}}), port).empty(),
          "a host name is refused (DNS rebinding)");
    CHECK(!remoteCheckRequest(req("GET", "/", {{"host", "192.168.1.20:47800"}}), port).empty(), "the wrong port is refused");
    CHECK(!remoteCheckRequest(req("GET", "/", {{"host", "192.168.1.20:47801"}, {"origin", "http://evil.example"}}), port).empty(),
          "another site's page is refused");
    CHECK(!remoteCheckRequest(req("POST", "/api/rate", {{"host", "192.168.1.20:47801"}}), port).empty(),
          "a POST without the page's header is refused");
    CHECK(remoteCheckRequest(req("POST", "/api/rate", {{"host", "192.168.1.20:47801"}, {"x-choosen", "1"}}), port).empty(),
          "the page's own POST is accepted");
    CHECK(remoteAllowedPath("POST", "/api/rate") && remoteAllowedPath("GET", "/api/items") && remoteAllowedPath("GET", "/thumb/3"),
          "phones may rate and browse");
    CHECK(!remoteAllowedPath("POST", "/api/config") && !remoteAllowedPath("POST", "/api/quit") &&
              !remoteAllowedPath("POST", "/api/pickfolder") && !remoteAllowedPath("POST", "/api/remote/pair") &&
              !remoteAllowedPath("POST", "/api/reveal") && !remoteAllowedPath("POST", "/api/export") &&
              !remoteAllowedPath("GET", "/api/export"),
          "phones cannot change settings, pair others, open folders, export files or quit");
    CHECK(remotePageFile("/") && remotePageFile("/app.js") && remotePageFile("/v3") && remotePageFile("/v3/") &&
              remotePageFile("/v3/app.js") && remotePageFile("/v3/app.css") && remotePageFile("/v3/index.html"),
          "an unpaired phone may load the pages (to show the pairing screen)");
    CHECK(!remotePageFile("/img/3") && !remotePageFile("/thumb/3") && !remotePageFile("/api/state") &&
              !remotePageFile("/v3/../img/3") && !remotePageFile("/v3/ratings.log") && !remotePageFile("/v3/x/app.js") &&
              !remotePageFile("/v3x/app.js"),
          "but no photo, API or other file without pairing");

    std::string token;
    CHECK(!remotePair("123456", token), "no pairing without a code shown on the PC");
    std::string code = remoteStartPairing();
    std::string wrong = code == "000000" ? "000001" : "000000";
    CHECK(code.size() == 6 && !remotePair(wrong, token), "a wrong code is refused");
    for (int i = 0; i < 4; ++i) remotePair(wrong, token);
    CHECK(!remotePair(code, token), "after 5 wrong tries the code no longer works");
    code = remoteStartPairing();
    const int before = remoteDeviceCount();
    CHECK(remotePair(code, token) && token.size() == 64 && remoteDeviceCount() == before + 1, "the right code pairs the phone");
    CHECK(!remotePair(code, token), "a code pairs one phone only");
    {
      // The same phone scans a new QR code: its old token is replaced, the count stays the same.
      const int count = remoteDeviceCount();
      const auto pairedOf = [] {
        const std::string s = remoteStateJson();
        const size_t p = s.find("\"paired\":");
        return p == std::string::npos ? -1 : atoi(s.c_str() + p + 9);
      };
      const int pairedBefore = pairedOf();
      std::string again;
      CHECK(remotePair(remoteStartPairing(), again, token) && again != token && remoteDeviceCount() == count &&
                !remoteAuthorized(req("GET", "/api/state", {{"cookie", "choosen_token=" + token}})) &&
                remoteAuthorized(req("GET", "/api/state", {{"cookie", "choosen_token=" + again}})),
            "a phone that pairs again keeps one registration");
      CHECK(pairedBefore >= 0 && pairedOf() == pairedBefore + 1, "the PC sees that a phone paired");
      remoteStartPairing();
      const std::string v1 = remoteStateJson(), v3 = remoteStateJson(true);
      CHECK(!has(v1, "\"link\"") || (has(v1, "/#pair=") && !has(v1, "v3/") && has(v3, "/v3/#pair=")),
            "the V3 settings' QR code opens the V3 page");
      CHECK(remoteToken(req("GET", "/", {{"cookie", "x=1; choosen_token=" + again + "; y=2"}})) == again &&
                remoteToken(req("GET", "/", {{"cookie", "xchoosen_token=" + again}})).empty(),
            "the token is read from its own cookie only");
      token = again;
    }
    CHECK(remoteAuthorized(req("GET", "/api/state", {{"cookie", "a=1; choosen_token=" + token}})), "its token opens the library");
    CHECK(!remoteAuthorized(req("GET", "/api/state", {{"cookie", "choosen_token=" + std::string(64, '0')}})) &&
              !remoteAuthorized(req("GET", "/api/state", {})),
          "an unknown or missing token does not");
    std::string saved;
    CHECK(readFileBytes(data + L"\\remote-devices.txt", saved) && has(saved, token.c_str()), "paired phones are stored");
    remoteForgetAll();
    CHECK(!remoteAuthorized(req("GET", "/", {{"cookie", "choosen_token=" + token}})) && remoteDeviceCount() == 0,
          "unpairing all phones locks them out");
  }

  say("== one app at a time on a data folder");
  {
    std::string holder;
    PlatLock* v1 = platLock(data, "Choosen V1 (pid 1)", holder);
    PlatLock* v2 = platLock(data, "Choosen V2 (pid 2)", holder);
    CHECK(v1 && !v2 && holder == "Choosen V1 (pid 1)", "a second app cannot open a data folder in use and learns which app has it");
    platUnlock(v1);
    v2 = platLock(data, "Choosen V2 (pid 2)", holder);
    CHECK(v2 != nullptr, "once the first app has closed, the other one can open it");
    platUnlock(v2);
  }

  say("== QR code for pairing a phone");
  {
    // Reed-Solomon: the worked example "HELLO WORLD", version 1-M (thonky.com QR tutorial).
    const std::vector<uint8_t> hello = {32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17};
    const std::vector<uint8_t> helloEcc = {196, 35, 39, 119, 235, 215, 231, 226, 93, 23};
    CHECK(qrErrorCorrection(hello, 10) == helloEcc, "error correction codewords match the published example");
    // Format information for level M, masks 0-7 (table of the standard).
    const int formatM[8] = {0b101010000010010, 0b101000100100101, 0b101111001111100, 0b101101101001011,
                            0b100010111111001, 0b100000011001110, 0b100111110010111, 0b100101010100000};
    bool formatOk = true;
    for (int m = 0; m < 8; ++m) formatOk = formatOk && qrFormatBits(m) == formatM[m];
    CHECK(formatOk, "format information matches the table of the standard");
    CHECK(qrEncode(std::string(200, 'x')).empty(), "a text too long for version 6 gives no symbol");

    // Reads a symbol back with its own copy of the rules (not the encoder's code): function
    // patterns, format bits -> mask, unmask, zigzag order, de-interleave, error check, bytes.
    auto decode = [](const std::vector<std::string>& rows, bool& eccOk) -> std::string {
      const int n = (int)rows.size(), ver = (n - 17) / 4;
      auto dark = [&](int x, int y) { return rows[y][x] == '1'; };
      auto isFunction = [&](int x, int y) {
        if ((x < 9 && y < 9) || (x >= n - 8 && y < 9) || (x < 9 && y >= n - 8)) return true;  // finders + format
        if (x == 6 || y == 6) return true;                                                       // timing
        return ver >= 2 && std::abs(x - (n - 7)) <= 2 && std::abs(y - (n - 7)) <= 2;              // alignment
      };
      int fmt = 0;  // first copy, bit i as placed by the standard
      const int fx[15] = {8, 8, 8, 8, 8, 8, 8, 8, 7, 5, 4, 3, 2, 1, 0};
      const int fy[15] = {0, 1, 2, 3, 4, 5, 7, 8, 8, 8, 8, 8, 8, 8, 8};
      for (int i = 0; i < 15; ++i) fmt |= (dark(fx[i], fy[i]) ? 1 : 0) << i;
      int mask = -1;
      for (int m = 0; m < 8; ++m)
        if (qrFormatBits(m) == fmt) mask = m;
      if (mask < 0) return "<no format>";
      auto masked = [&](int x, int y) {
        switch (mask) {
          case 0: return (x + y) % 2 == 0;
          case 1: return y % 2 == 0;
          case 2: return x % 3 == 0;
          case 3: return (x + y) % 3 == 0;
          case 4: return (x / 3 + y / 2) % 2 == 0;
          case 5: return (x * y) % 2 + (x * y) % 3 == 0;
          case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
          default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
        }
      };
      std::vector<int> bits;
      for (int col = n - 1; col > 0; col -= 2) {  // pairs of columns, right to left, skipping column 6
        if (col == 6) col = 5;
        const bool up = ((n - 1 - col) / 2) % 2 == 0 ? true : false;
        for (int k = 0; k < n; ++k) {
          const int y = up ? n - 1 - k : k;
          for (int x : {col, col - 1})
            if (!isFunction(x, y)) bits.push_back(dark(x, y) != masked(x, y) ? 1 : 0);
        }
      }
      std::vector<uint8_t> words(bits.size() / 8, 0);
      for (size_t i = 0; i < words.size() * 8; ++i) words[i / 8] |= (uint8_t)(bits[i] << (7 - i % 8));
      const int eccPer[7] = {0, 10, 16, 26, 18, 24, 16}, blocks[7] = {0, 1, 1, 1, 2, 2, 4};
      const int k = blocks[ver], e = eccPer[ver];
      const int totalWords = (int)words.size(), dataPer = totalWords / k - e;
      std::vector<std::vector<uint8_t>> data(k), ecc(k);
      for (int i = 0; i < dataPer * k; ++i) data[i % k].push_back(words[i]);
      for (int i = 0; i < e * k; ++i) ecc[i % k].push_back(words[dataPer * k + i]);
      eccOk = true;
      std::vector<uint8_t> all;
      for (int b = 0; b < k; ++b) {
        eccOk = eccOk && qrErrorCorrection(data[b], e) == ecc[b];
        all.insert(all.end(), data[b].begin(), data[b].end());
      }
      if ((all[0] >> 4) != 4) return "<not byte mode>";
      const int len = ((all[0] & 15) << 4) | (all[1] >> 4);
      std::string out;
      for (int i = 0; i < len; ++i) out += (char)(((all[1 + i] & 15) << 4) | (all[2 + i] >> 4));
      return out;
    };
    for (const std::string text : {std::string("http://192.168.15.2:47801/#pair=123456"), std::string("Choosen"),
                                   std::string("http://192.168.100.200:47801/#pair=987654&x=0123456789abcdef")}) {
      const auto rows = qrEncode(text);
      const int n = (int)rows.size();
      bool square = n >= 21 && (n - 17) % 4 == 0;
      for (auto& r : rows) square = square && (int)r.size() == n;
      CHECK(square, "\"%s\": a square symbol of a valid size (%d)", text.c_str(), n);
      if (!square) continue;
      bool finders = true;
      const char* finder[7] = {"1111111", "1000001", "1011101", "1011101", "1011101", "1000001", "1111111"};
      for (int y = 0; y < 7; ++y) {
        finders = finders && rows[y].substr(0, 7) == finder[y] && rows[y].substr(n - 7) == finder[y] &&
                  rows[n - 7 + y].substr(0, 7) == finder[y];
      }
      bool timing = true;
      for (int i = 8; i < n - 8; ++i) timing = timing && rows[6][i] == (i % 2 == 0 ? '1' : '0') && rows[i][6] == (i % 2 == 0 ? '1' : '0');
      CHECK(finders && timing && rows[n - 8][8] == '1', "\"%s\": finder, timing and dark module in place", text.c_str());
      bool eccOk = false;
      const std::string back = decode(rows, eccOk);
      CHECK(back == text && eccOk, "\"%s\" reads back (got \"%s\", error check %s)", text.c_str(), back.c_str(),
            eccOk ? "ok" : "wrong");
    }
  }

  say("== page built into the exe");
  {
    std::string html, js, css, png, none;
    CHECK(webResource("index.html", html) && has(html, "app.js") && has(html, "app.css"), "index.html is built in");
    CHECK(webResource("app.js", js) && has(js, "function act(") && webResource("app.css", css) && css.size() > 1000,
          "app.js and app.css are built in");
    CHECK(webResource("icon.png", png) && png.rfind("\x89PNG", 0) == 0, "icon.png is built in");
    CHECK(!webResource("ratings.log", none), "nothing else is served from the exe");
    std::string v3html, v3js, v3css;
    CHECK(webResource("v3/index.html", v3html) && has(v3html, "/v3/app.js") && webResource("v3/app.js", v3js) &&
              v3js.size() > 10000 && webResource("v3/app.css", v3css) && !v3css.empty(),
          "Choosen V3 is built in");
    CHECK(web3Name("app.js") && !web3Name("../index.html") && !web3Name("v3/app.js") && !web3Name("ratings.log") &&
              !web3Name("") && !web3Name("App.js"),
          "only V3's own three files are served at /v3/");
  }

  coreResetForTest();
  rmTree(base);
  say("\nselftest: %d passed, %d failed", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// Synthetic library written straight into index.tsv (no real files needed).
void makeSyntheticIndex(const std::wstring& data, const std::string& root, int nItems, int nFolders,
                        std::vector<std::string>& rels, std::vector<std::string>& fps) {
  std::string s = "#choosen-index-1\t" + root + "\n";
  std::mt19937_64 r(42);
  rels.clear();
  fps.clear();
  for (int i = 0; i < nItems; ++i) {
    int f = (int)(r() % (uint64_t)nFolders);
    std::string fp = hex64(r() | 1);
    std::string rel = "Artist" + std::to_string(f) + " (" + std::to_string(100000 + f) + ")/" +
                      std::to_string(10000000 + i) + "_p0 - title.png";
    s += fp + "\t" + std::to_string(100000 + i) + "\t" + std::to_string(133000000000000000ULL + i) + "\t" + rel + "\n";
    rels.push_back(rel);
    fps.push_back(fp);
  }
  writeFileAtomic(data + L"\\index.tsv", s);
}

std::string jsonField(const std::string& s, const char* key, size_t from = 0) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = s.find(k, from);
  if (p == std::string::npos) return "";
  p += k.size();
  if (s[p] == '"') {
    size_t e = s.find('"', p + 1);
    return s.substr(p + 1, e - p - 1);
  }
  size_t e = s.find_first_of(",}]", p);
  return s.substr(p, e - p);
}

int bench() {
  std::wstring base = tempRoot(L"bench");
  rmTree(base);
  std::wstring data = base + L"\\data";
  mkdirs(data);
  g_dataDir = data;
  g_cfg = Config{};
  g_cfg.root = "Z:\\choosen-bench-library";
  std::vector<std::string> rels, fps;
  const int N = 150000;
  makeSyntheticIndex(data, g_cfg.root, N, 3000, rels, fps);
  // 50,000 existing ratings
  {
    std::mt19937_64 r(7);
    std::string j;
    for (int t = 0; t < 50000; ++t) {
      int i = (int)(r() % N);
      const char a = "LLSDD"[r() % 5];
      j += std::to_string(t) + "\t" + std::to_string(1700000000000LL + t) + "\t" + fps[i] + "\t" + a + "\tZ:\\x\tcid=b" +
           std::to_string(t) + "\n";
    }
    writeFileAtomic(data + L"\\ratings.log", j);
  }
  coreResetForTest();
  double t0 = nowSec();
  coreInit();
  double tInit = nowSec() - t0;
  say("library: %d images in 3000 folders, 50000 ratings in the journal", N);
  say("startup (index + journal + scores): %.0f ms", tInit * 1000);

  auto timeIt = [&](const char* name, int reps, auto fn) {
    double a = nowSec();
    size_t bytes = 0;
    for (int i = 0; i < reps; ++i) bytes += fn(i);
    double ms = (nowSec() - a) * 1000 / reps;
    say("%-34s %8.2f ms   (%zu bytes)", name, ms, bytes / reps);
  };
  timeIt("apiState", 50, [](int) { return apiState().size(); });
  timeIt("apiNext n=6", 50, [](int) { return apiNext("", "", "", 6).size(); });
  timeIt("apiNext n=1 scoped to 3 folders", 50,
         [](int) { return apiNext("Artist1 (100001)|Artist2 (100002)|Artist3 (100003)", "", "", 1).size(); });
  timeIt("apiFolders", 20, [](int) { return apiFolders().size(); });
  timeIt("apiItems liked, sort=score", 20, [](int) { return apiItems("liked", "", "score", 0, 150).size(); });
  timeIt("apiItems all, sort=folder", 10, [](int) { return apiItems("all", "", "folder", 0, 150).size(); });
  g_journalFlush = true;
  timeIt("apiRate (incl. fsync, temp on C:)", 100, [&](int i) {
    int st = 200;
    return apiRate(i, fps[i], 'L', "bench" + std::to_string(i), "", st).size();
  });
  g_journalFlush = false;
  timeIt("apiRate (no fsync)", 100, [&](int i) {
    int st = 200;
    return apiRate(1000 + i, fps[1000 + i], 'D', "benchx" + std::to_string(i), "", st).size();
  });
  coreResetForTest();
  rmTree(base);
  return 0;
}

// ---------------------------------------------------------------------------------------------
// Long-run scheduler behaviour: a synthetic owner with stable tastes swipes through a library the
// size of the real one. Reports how many cards are reviews, how big the review backlog gets, and
// how late reviews come back compared with their planned interval.
int simulate() {
  std::wstring base = tempRoot(L"simulate");
  rmTree(base);
  std::wstring data = base + L"\\data";
  mkdirs(data);
  g_dataDir = data;
  g_cfg = Config{};
  g_cfg.root = "Z:\\choosen-sim-library";
  std::vector<std::string> rels, fps;
  const int N = 53000, F = 2000;
  makeSyntheticIndex(data, g_cfg.root, N, F, rels, fps);
  std::map<std::string, double> quality;  // latent "how much the owner likes it", artist mean + noise
  {
    std::mt19937_64 r(99);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::vector<double> artist(F);
    for (auto& a : artist) a = 0.55 + 0.18 * nd(r);
    for (int i = 0; i < N; ++i) {
      int f = atoi(rels[i].c_str() + 6);
      quality[fps[i]] = std::clamp(artist[f] + 0.2 * nd(r), 0.0, 1.0);
    }
  }
  coreResetForTest();
  coreInit();
  auto env = [](const char* k, double d) {
    char b[64];
    DWORD n = GetEnvironmentVariableA(k, b, sizeof b);
    return n && n < sizeof b ? atof(b) : d;
  };
  double pMin = env("SIM_SHARE_MIN", 0.3), pMax = env("SIM_SHARE_MAX", 0.7), pHalf = env("SIM_BACKLOG_HALF", 300),
         pPass = env("SIM_PASS_WEIGHT", 0.5), pLate = env("SIM_LATE_CAP", 3.0);
  coreSetPolicy(pMin, pMax, pHalf, pPass, pLate);
  say("policy: review share %.2f..%.2f (half at %.0f waiting), pass weight %.2f, late credit cap %.1f", pMin, pMax, pHalf,
      pPass, pLate);
  g_journalFlush = false;
  std::map<std::string, uint64_t> lastSeen;  // fp -> tick of its previous rating
  std::vector<int64_t> gapLiked, gapPassed, gap1Liked, gap1Passed;
  std::mt19937_64 r(5);
  std::uniform_real_distribution<double> U(0, 1);
  const int SWIPES = 30000;
  int reviews = 0, ahead = 0, reviewLiked = 0, reviewPassed = 0;
  std::vector<int64_t> lateness;
  say("%8s %9s %9s %9s %10s %10s %12s %12s", "swipes", "reviews%", "backlog", "rated", "late(med)", "late(p90)",
      "gapLike(med)", "gapPass(med)");
  for (int s = 1; s <= SWIPES; ++s) {
    std::string nx = apiNext("", "", "", 1);
    size_t at = nx.find("\"items\":[{");
    if (at == std::string::npos) break;
    int id = atoi(jsonField(nx, "id", at).c_str());
    std::string fp = jsonField(nx, "fp", at);
    std::string kind = jsonField(nx, "kind", at);
    std::string last = jsonField(nx, "last", at);
    if (kind == "review") {
      reviews++;
      lateness.push_back(-_atoi64(jsonField(nx, "dueIn", at).c_str()));
      int64_t gap = (int64_t)s - (int64_t)lastSeen[fp];
      bool first = jsonField(nx, "n", at) == "1";
      if (last == "D") reviewPassed++, gapPassed.push_back(gap), first ? gap1Passed.push_back(gap) : void();
      else reviewLiked++, gapLiked.push_back(gap), first ? gap1Liked.push_back(gap) : void();
    } else if (kind == "ahead") {
      ahead++;
    }
    double q = quality[fp];
    double u = U(r);
    char a = u < 0.03 ? 'K' : (U(r) < q ? (q > 0.85 ? 'S' : 'L') : 'D');
    int st = 200;
    apiRate(id, fp, a, "s" + std::to_string(s), "", st);
    if (a != 'K') lastSeen[fp] = (uint64_t)s;
    if (s % 2500 == 0) {
      std::sort(lateness.begin(), lateness.end());
      int64_t med = lateness.empty() ? 0 : lateness[lateness.size() / 2];
      int64_t p90 = lateness.empty() ? 0 : lateness[lateness.size() * 9 / 10];
      std::string stt = apiState();
      auto median = [](std::vector<int64_t>& v) {
        if (v.empty()) return (int64_t)0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
      };
      say("%8d %8.1f%% %9s %9s %10lld %10lld %12lld %12lld", s, 100.0 * reviews / 2500, jsonField(stt, "due").c_str(),
          jsonField(stt, "rated").c_str(), (long long)med, (long long)p90, (long long)median(gapLiked),
          (long long)median(gapPassed));
      reviews = 0;
      lateness.clear();
      gapLiked.clear();
      gapPassed.clear();
      if (s != SWIPES) {
        gap1Liked.clear();
        gap1Passed.clear();
      }
    }
  }
  auto med = [](std::vector<int64_t>& v) {
    if (v.empty()) return (int64_t)0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  say("reviews of liked images: %d, of passed images: %d, early ('ahead') cards: %d", reviewLiked, reviewPassed, ahead);
  say("first review comes back after (median): liked %lld cards, passed %lld cards (last 2500 swipes)",
      (long long)med(gap1Liked), (long long)med(gap1Passed));
  coreResetForTest();
  rmTree(base);
  return 0;
}

}  // namespace

int runTool(const std::wstring& tool) {
  // Output: console if started from one, plus build\selftest.txt next to the exe.
  bool console = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
  int rc = 0;
  double t0 = nowSec();
  if (tool == L"--selftest") rc = selftest();
  else if (tool == L"--bench") rc = bench();
  else if (tool == L"--simulate") rc = simulate();
  say("(%.1f s)", nowSec() - t0);
  std::wstring outDir = g_baseDir + L"\\build";
  CreateDirectoryW(outDir.c_str(), nullptr);
  writeFileAtomic(outDir + L"\\selftest.txt", g_out);
  if (console) {
    DWORD put = 0;
    std::wstring w = widen(g_out);
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), w.data(), (DWORD)w.size(), &put, nullptr);
  }
  return rc;
}
