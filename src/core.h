// Library index, rating journal, "reverse SRS" scheduler and folder ranking.
#pragma once
#include "common.h"

constexpr int kMaxSubfolderDepth = 64;  // largest per-folder subfolder limit; the page gets it from /api/state

struct Config {
  std::string root;                                  // first photo folder
  std::vector<std::string> moreRoots;                // further photo folders, in priority order
  std::vector<int> maxDepths;                        // per photo folder (root, then moreRoots): subfolder
                                                     // levels read; -1 = all, 0 = only images directly in it
  int artistDepth = 1;                               // folder depth treated as "artist"
  int mouseMode = 1;                                 // 1 = drag/throw, 2 = wheel/click, 3 = flick, 4 = custom
  int keyMode = 1;                                   // 1 = A (classic), 2 = B (arrows), 3 = C (J/K/L), 4 = custom
  int port = 47800;
  std::string ignore = ".*;__pycache__;node_modules";  // folder names skipped while scanning
  std::string backupDir;                             // optional second backup folder (another drive)
  double reviewShare = 0.1;                          // share of cards used for re-evaluation (0..1)
  bool groupWorks = false;                           // one card per Pixiv work (<id>_p0, _p1, ...)
  bool swipeButtons = true;                          // Version 2: the round rating buttons on the card
  bool advanced = false;                             // settings show the detailed options
  bool throwSlow = false;                            // Fast: a throw counts at once; Slow: 0.5 s to redirect
  int throwDelayMs = 300;                            // advanced mode: redirect window in ms
  int longPressMs = 400;                             // advanced mode: hold time that turns like into super
  bool overPoints = true;                            // show points above 100 as "100 (+N)"
  bool remote = false;                               // phones on the same network may connect (paired only)
  std::string customKeys;                            // keyboard layout 4, e.g. "like:ArrowRight,KeyL;pass:ArrowLeft"
  std::string customMouse;                           // mouse mode 4, e.g. "left:grab;right:back;wheelUp:back"
};
extern Config g_cfg;

void loadConfig();
void saveConfig();
// What the page uses: advanced mode picks the exact values, otherwise Fast/Slow and 0.4 s.
int effectiveThrowMs();
int effectiveHoldMs();

void coreInit();       // load index cache + journal, compute scores
void coreDailyBackup();  // copies the journal again when the date changed since the last copy
void coreStartScan();  // (re)scan the root folder in the background
// selftest only: f runs while a rating is being written to the journal (nullptr: nothing)
void coreDebugJournalHook(void (*f)());
void coreSaveIndex();
uint64_t coreVersion();

// Settings change; unset fields keep their value.
struct ConfigPatch {
  const std::string* root = nullptr;
  const std::vector<std::string>* moreRoots = nullptr;  // replaces the whole list
  const std::vector<int>* maxDepths = nullptr;          // one per folder of root + moreRoots as sent
  const std::string* backupDir = nullptr;
  int artistDepth = 0;
  int mouseMode = 0;
  int keyMode = 0;
  double reviewShare = -1;
  int groupWorks = -1;
  int swipeButtons = -1;
  int throwDelayMs = -1;
  int longPressMs = -1;
  int advanced = -1;
  int throwSlow = -1;
  int overPoints = -1;
  int remote = -1;
  const std::string* customKeys = nullptr;
  const std::string* customMouse = nullptr;
};
// Applies settings; returns an error message (Japanese) or empty on success. Nothing is applied
// when validation fails.
std::string coreSetConfig(const ConfigPatch& patch);

std::string apiState();
// Images of a saved photo folder that a subfolder limit of maxDepth would hide (for the settings
// preview before saving).
int coreHiddenBy(const std::string& rootPath, int maxDepth);
std::string apiFolders(int64_t sinceMs = 0);  // sinceMs: compare ranks with this moment (0 = session start)
std::string apiStats(int days);
// filter: "" normal schedule; "fresh" unrated only; "liked" / "super" / "passed" = re-swipe cards
// of that class (least recently rated first).
std::string apiNext(const std::string& scope, const std::string& exclude, const std::string& recent, int n,
                    const std::string& filter = "");
// filter: all, liked, super, passed, unrated, rated, archived. seed reshuffles sort=random.
std::string apiItems(const std::string& filter, const std::string& scope, const std::string& sort, int offset,
                     int limit, uint64_t seed = 0);
// One card per artist-level folder with a cover image, counting the cards that match `filter`.
std::string apiFolderCards(const std::string& filter, const std::string& scope, const std::string& sort);
// action: L like, S super like, D pass, K hold, A archive (never shown again), R restore.
// fpHex is required: ratings always name the file by fingerprint so a stale id can never hit
// another photo. cid makes retries idempotent; replaceCid names the rating a correction replaces.
std::string apiRate(int id, const std::string& fpHex, char action, const std::string& cid,
                    const std::string& replaceCid, int& status);
// Withdraws one rating (the page's cancel): its events are revoked as a correction revokes them,
// and nothing takes their place. An unknown or already withdrawn cid is answered with 0 (a retry).
std::string apiRevoke(const std::string& cid, int& status);
// Folder reset, started by the owner: every like, super like, pass and hold of the images in
// `scope` (scope tokens as for /api/next; never empty) is revoked, so they are unrated again;
// archived images stay archived. Written as revocations, so older versions read it the same way.
// dryRun only counts {"images","ratings"}.
std::string apiResetRatings(const std::string& scope, bool dryRun, int& status);
// The sub gallery (サブギャラリー): pictures the owner liked while the gallery played as a story.
// Kept by fingerprint in data\picks.tsv ("<fp>\t<ms>" lines); the pictures are never copied.
// apiPicks: {"items":[cards]} newest first, pictures that are present only.
std::string apiPicks();
// add (or remove) one picture by its fingerprint; adding one that is there already does nothing.
std::string apiPick(const std::string& fpHex, bool add, int& status);
void coreForgetPicksForTest();  // the self-test reads picks.tsv again

// Absolute path of an item, for serving and "show in Explorer". fpHex (optional) wins over id.
bool itemFile(int id, const std::string& fpHex, std::wstring& path, uint64_t& fp, std::string& ext);

// Export (書き出し, core_export.cpp): copies or moves the images of one kind into a new folder
// "<to>\Choosen <kind> <date>", keeping their subfolders. Started only by the user; nothing is
// overwritten. what: archived, passed, liked (likes and super likes), likeOnly, super.
// Returns "" when the job started, else the reason (Japanese, for the screen).
std::string coreExportStart(const std::string& what, const std::string& to, bool move);
// {"running","done","failed","total","move","what","folder"} of the current or last job.
std::string coreExportState();
// Files a job would handle now (a move also takes the byte-identical copies of those images).
int coreExportCount(const std::string& what, bool move);

// Sorting into subfolders (評価ごとにサブフォルダへ, core_export.cpp): moves the pictures of the given
// kinds (passed, likeOnly, super; comma separated) into a folder named after the kind (見送り, 好評,
// 超好評) next to each picture, for the folders of "scope" or the item ids in "ids" (one of the two is
// needed: never the whole library). Shares the export's job slot and progress (what "sort" / "undo").
// Count: {"passed","likeOnly","super","undo" (moves of the last sort that can be undone)}.
// One picture's ratings in order with the points after each (the gallery's full view).
std::string apiHistory(int id, const std::string& fpHex);
std::string coreSortCount(const std::string& scope, const std::string& ids, const std::string& kinds);
std::string coreSortStart(const std::string& scope, const std::string& ids, const std::string& kinds);  // "" = started
std::string coreSortUndo();   // moves the last sort back; "" = started
std::string coreSortMoves();  // {"moves":[[from,to],...]} of the last sort (the phone updates its gallery)

// ---- hooks for the developer tools in selftest.cpp -----------------------------------------
struct DebugItem {
  int id = -1;
  std::string rel, fp;
  int root = 0;  // index of its photo folder (0 = g_cfg.root)
  bool present = false, rated = false, archived = false;
  double p = 0, S = 0;
  int64_t dueIn = 0;
  int n = 0, events = 0, nL = 0, nS = 0;
  char last = 0;
};
struct DebugFolder {
  std::string path;
  int depth = 0, total = 0, images = 0, rated = 0, liked = 0, rank = 0;
  bool artist = false;
  double score = 0;
};
extern bool g_journalFlush;  // the simulator turns off fsync to run fast
void coreResetForTest();
void coreScanSync();
bool coreDebugItem(const std::string& rel, DebugItem& out);
bool coreDebugFolder(const std::string& path, DebugFolder& out);
uint64_t coreDebugTick();
int coreDebugHashed();  // files the last scan had to fingerprint
int coreDebugCopies();  // files known to be copies of an image
// Places one file the way an export does (never overwriting: "name (2).ext" when the name is taken).
// Returns the path used, "" if it failed.
std::string coreDebugExportPlace(const std::string& from, const std::string& to, bool move);
void coreSetPolicy(double shareMin, double shareMax, double backlogHalf, double passWeight, double lateCap);
// Replays a rating history (e.g. "LLD" at the given ticks) through the scheduler; dueIn is
// measured from the last event.
DebugItem coreDebugSchedule(const std::string& actions, const std::vector<uint64_t>& ticks);
