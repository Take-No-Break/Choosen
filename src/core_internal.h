// Internal state and helpers shared by core*.cpp (not for other files; the public interface is
// core.h). Nothing here uses the operating system: that goes through platform.h.
#pragma once
#include "core.h"
#include "platform.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace core {

// ---- scheduling / scoring constants -------------------------------------------------
// Intervals are counted in cards (swipes), not days: "reappears a few hundred cards later".
const double kBaseInterval = 150.0;
const double kGrowLike = 2.5;   // right swipe
const double kGrowSuper = 3.0;  // super like
const double kGrowPass = 4.0;   // left swipe: always comes back later than a like would
const double kFlipKeep = 0.4;   // passed before, liked now: check again sooner
const double kMaxStability = 1e5;  // caps intervals (and keeps the tick arithmetic far from overflow)
const double kSkipInterval = 30.0;
// Image points (sorting and display only; never stored, always replayed from the journal):
// the first like gives 67 and every further like +2; the first super like lifts the image to 86
// (or +2 if it already had more) and from then on a like adds +1 and a super like +2. A pass
// changes nothing. There is no upper limit; the page may show values above 100 as "100 (+N)".
const double kPtsLike = 67, kPtsSuper = 86;
const double kAddLike = 2, kAddLikeOnSuper = 1, kAddSuper = 2;
const double kWilsonZ = 1.645;     // folder score = 90% lower bound of the right-swipe share

struct Event {
  uint64_t tick;
  int64_t ms;
  char a;  // L like, S super like, D pass (left), K skip (hold)
};

struct Item {
  uint64_t fp = 0;
  std::string rel;  // relative to its photo folder, '/' separated
  int root = 0;     // index into photoRoots()
  int64_t size = 0;
  uint64_t mtime = 0;
  bool present = false;
  int seenGen = 0;
  int folder = -1;
  std::vector<Event> ev;
  // derived
  bool touched = false, rated = false, hold = false;
  bool archived = false;  // "never show again": out of every card list, ranking and count
  double S = 1, p = 0;  // p: points (see kPtsLike)
  uint64_t due = 0, lastRateTick = 0;
  int nRatings = 0, nLike = 0, nSuper = 0;
  char last = 0;  // latest rating (L, S or D); holds do not change it
  int64_t lastMs = 0;
  int page = 0;   // page number within its work (Pixiv "_p<N>")
  int work = -1;  // index into `works`
};

// A card. With work grouping on, all pages of one Pixiv work (<id>_p0, <id>_p1, ...) in the same
// folder form one work: one card, one swipe, one vote for the folder. Otherwise every image is
// its own work. Ratings are still journaled per file, so identity and history stay per image.
struct Work {
  int folder = -1;
  std::vector<int> pages;  // item indices, sorted by page number; pages[0] is the cover
};

struct Folder {
  std::string name, path;
  int parent = -1, depth = 0;
  int root = 0;  // photo folder (index into photoRoots())
  int total = 0, images = 0, rated = 0, liked = 0, supers = 0;  // total/rated/liked count cards
  double score = 0;
  int rank = 0, rank0 = 0;  // rank0: rank when this session started
};

// Scheduler knobs; the simulator (--simulate) can override them from the environment.
// With tens of thousands of images, rated images always outnumber the review slots, so the
// scheduler works on a budget: at most reviewShare of the cards are reviews (settable in the UI),
// and within that budget the image most overdue relative to its own interval comes first.
struct Policy {
  double reviewShareMin = 0.1;  // share of cards that are reviews when few are waiting
  double reviewShareMax = 0.1;  // ... and when many are waiting (equal = fixed budget)
  double backlogHalf = 300;      // waiting reviews at which the share is halfway between the two
  double passWeight = 0.35;      // overdue passes wait behind overdue likes
  double lateCap = 3.0;          // a late review credits at most this many planned intervals
};

// Every rating carries a client id (cid). It makes retried POSTs idempotent and lets a
// correction name exactly which earlier rating it replaces, even across restarts.
struct CidRef {
  uint64_t fp;
  uint64_t tick;
};

struct Found {
  std::string rel;
  int root = 0;
  int64_t size;
  uint64_t mtime;
  uint64_t fp;
  bool force = false;  // take the image over without the copy check (its old file is gone or changed)
};

// A file whose bytes equal an image of the library (checked in full): it is not a card of its own.
// Remembered in the index cache, so it is neither fingerprinted nor compared again while unchanged.
struct Copy {
  int root = 0;
  std::string rel;
  int64_t size = 0;
  uint64_t mtime = 0;
  uint64_t fp = 0;  // the image it is a copy of
  int seenGen = 0;
};

// ---- shared state (defined in core.cpp) ------------------------------------------------
extern Policy g_policy;
extern std::mutex mu;
extern std::mutex jmu;  // orders journal appends; always taken while holding mu, released after the write
// One rating change at a time (apiRate, apiRevoke, apiResetRatings), from choosing what it replaces
// to the change in memory: mu is let go during the journal write, and without this a second change
// could pick the same rating to replace meanwhile. Always taken before mu.
extern std::mutex rateMu;
extern void (*g_journalHook)();  // selftest only: runs during a rating's journal write
extern std::unordered_map<std::string, std::vector<CidRef>> cidMap;  // one rating
extern std::unordered_set<std::string> pendingCids;                  // ratings being written right now
extern std::string backupWarning;
extern std::vector<Item> items;
extern std::vector<Work> works;
extern std::unordered_map<uint64_t, int> byFp;
extern std::vector<Folder> folders;
extern std::unordered_map<std::string, int> folderByPath;
extern uint64_t tick;
extern int sessionSwipes;
extern uint64_t sessionSeed;
extern std::string rootName;
extern std::atomic<uint64_t> version;
extern std::atomic<bool> scanning;
extern std::atomic<int> scanGen, scanFound, scanHashed, scanToHash, scanDupes, scanSplit;
extern std::vector<Copy> copies;
extern std::vector<int> rootDeepest;  // deepest folder level that holds images, per photo folder
extern std::string scanError;
extern uint64_t sessionTick0;  // first tick of this session; ranks before it are the baseline
extern bool baselineDirty;
extern int groupedCount;  // how many cards there would be with grouping on (shown in settings)
extern std::atomic<int> lastBackupDay;
extern std::vector<std::string> g_unknownConfig;

// ---- functions shared between the core files -------------------------------------------
std::wstring journalPath();
std::wstring indexPath();
std::wstring index2Path();
std::vector<std::string> photoRoots();
int maxDepthOf(int root);
int artistLevelOf(int root);
bool isArtist(const Folder& f);
std::string rootPath(int root);
uint64_t mix64(uint64_t x);
double jitter(uint64_t fp, int k);
std::string slashToBack(std::string s);
std::string absPathOf(const Item& it);
std::wstring absPath(const Item& it);
std::string relFromAbs(const std::string& abs, int& rootOut);
std::string extOf(const std::string& rel);
void computeItem(Item& it);
double wilsonLower(int liked, int n);
void rankFolders(const std::vector<double>& score, const std::vector<int>& rated, const std::vector<int>& supers,
                 std::vector<int>& rankOut);
int leadOf(const Work& w);
void computeFolders();
void ranksBefore(bool byTime, uint64_t limit, std::vector<int>& rank, std::vector<double>& score,
                 std::vector<int>& rated);
void computeBaseline();
std::string localDay(int64_t ms);
bool isTopKey(const std::string& path);
std::string folderKeyOf(const Item& it);
int ensureFolder(const std::string& path);
bool pixivPage(const std::string& name, std::string& key, int& page);
void rebuildWorks();
void rebuildFolders();
std::vector<char> scopeMask(const std::string& scope);
std::string baseName(const std::string& rel);
std::string itemJson(int i, const char* kind, int stateOf = -1, const std::vector<int>* pages = nullptr);
void addIndexed(const std::vector<std::string>& f, size_t relCol, int root);
void loadIndex();
std::string journalField(const std::vector<std::string>& f, const char* key);
void loadJournal();
void dropTornTail();
bool appendJournal(const std::string& lines);
int64_t fileSize(const std::wstring& p);
std::vector<std::wstring> listBackups(const std::wstring& dir);
int todayNumber();
std::string backupTo(const std::wstring& dir, size_t keep);
void backupJournal();
bool isImageName(const std::wstring& n);
bool ignoredDir(const DirEntry& e, const std::vector<std::string>& ign);
void enumerate(const std::wstring& root, const std::string& relDir, std::vector<Found>& out,
               const std::vector<std::string>& ign, int gen, int levels);
uint64_t fingerprint(const std::wstring& path, int64_t size);
void mergeFound(std::vector<Found>& batch, int gen, std::vector<Found>& check);
int sameContent(const std::wstring& a, const std::wstring& b);
uint64_t contentId(const std::wstring& path, int64_t size);
void checkCopies(std::vector<Found> check, int gen, const std::vector<std::wstring>& roots);
void promoteCopies(int gen, const std::vector<std::wstring>& roots);
void scanThread(int gen);
std::string rootNameOf(const std::string& root);
bool cardMatches(const std::string& filter, const Item& it);
std::vector<int> artistFolders();
std::vector<std::vector<int>> archivedGroups(const std::vector<char>& inScope);
int resolveItem(int id, const std::string& fpHex);
bool validCid(const std::string& c);

}  // namespace core
