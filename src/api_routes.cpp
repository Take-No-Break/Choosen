#include "api_routes.h"

#include "common.h"
#include "core.h"
#include "qr.h"

#include <vector>

namespace {

std::string urlDecodeStr(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      o += ' ';
    } else if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

}  // namespace

void parseQueryInto(const std::string& query, std::unordered_map<std::string, std::string>& out) {
  if (query.empty()) return;
  for (auto& kv : split(query, '&')) {
    if (kv.empty()) continue;
    size_t eq = kv.find('=');
    if (eq == std::string::npos)
      out[urlDecodeStr(kv)] = "";
    else
      out[urlDecodeStr(kv.substr(0, eq))] = urlDecodeStr(kv.substr(eq + 1));
  }
}

bool apiRoute(const ApiCall& q, int& status, std::string& body) {
  const std::string& p = q.path;
  const bool post = q.method == "POST";
  if (p == "/api/state") {
    body = apiState();
    return true;
  }
  if (p == "/api/folders") {
    body = apiFolders(std::strtoll(q.param("since", "0").c_str(), nullptr, 10));
    return true;
  }
  if (p == "/api/stats") {
    body = apiStats(q.paramInt("days", 30));
    return true;
  }
  if (p == "/api/next") {
    body = apiNext(q.param("scope"), q.param("exclude"), q.param("recent"), q.paramInt("n", 8), q.param("filter"));
    return true;
  }
  if (p == "/api/items") {
    body = apiItems(q.param("filter", "liked"), q.param("scope"), q.param("sort", "score"), q.paramInt("offset", 0),
                    q.paramInt("limit", 120), std::strtoull(q.param("seed", "0").c_str(), nullptr, 10));
    return true;
  }
  if (p == "/api/folderCards") {
    body = apiFolderCards(q.param("filter", "liked"), q.param("scope"), q.param("sort", "rank"));
    return true;
  }
  if (p == "/api/picks") {  // GET: the sub gallery; POST ?fp=&add=1|0: one picture in or out
    body = post ? apiPick(q.param("fp"), q.paramInt("add", 1) != 0, status) : apiPicks();
    return true;
  }
  if (p == "/api/revoke" && post) {
    body = apiRevoke(q.param("cid"), status);
    return true;
  }
  if (p == "/api/reset") {  // GET: how many; POST: reset (the page asks the owner first)
    body = apiResetRatings(q.param("scope"), !post, status);
    return true;
  }
  if (p == "/api/rate" && post) {
    std::string a = q.param("a");
    body = apiRate(q.paramInt("id", -1), q.param("fp"), a.size() == 1 ? a[0] : '?', q.param("cid"), q.param("replace"),
                   status);
    return true;
  }
  if (p == "/api/config" && post) {
    std::string root = q.param("root"), backup = q.param("backup_dir");
    std::string keys = q.param("custom_keys"), mouse = q.param("custom_mouse");
    // more_roots: the further photo folders separated by '|', which Windows paths cannot contain
    std::vector<std::string> more;
    for (auto& m : split(q.param("more_roots"), '|'))
      if (!m.empty()) more.push_back(m);
    // max_depths: one subfolder limit per folder of root + more_roots, comma separated (-1 = all)
    std::vector<int> depths;
    for (auto& d : split(q.param("max_depths"), ','))
      if (!d.empty()) depths.push_back(atoi(d.c_str()));
    ConfigPatch c;
    if (q.params.count("root")) c.root = &root;
    if (q.params.count("more_roots")) c.moreRoots = &more;
    if (q.params.count("max_depths")) c.maxDepths = &depths;
    if (q.params.count("backup_dir")) c.backupDir = &backup;
    if (q.params.count("custom_keys")) c.customKeys = &keys;
    if (q.params.count("custom_mouse")) c.customMouse = &mouse;
    c.advanced = q.paramInt("advanced_mode", -1);
    c.throwSlow = q.paramInt("throw_slow", -1);
    c.overPoints = q.paramInt("over_points", -1);
    c.artistDepth = q.paramInt("artist_depth", 0);
    c.mouseMode = q.paramInt("mouse_mode", 0);
    c.keyMode = q.paramInt("key_mode", 0);
    if (q.params.count("review_share")) c.reviewShare = atof(q.param("review_share").c_str());
    c.groupWorks = q.paramInt("group_works", -1);
    c.swipeButtons = q.paramInt("swipe_buttons", -1);
    c.throwDelayMs = q.paramInt("throw_delay_ms", -1);
    c.longPressMs = q.paramInt("long_press_ms", -1);
    c.remote = q.paramInt("remote_access", -1);
    std::string err = coreSetConfig(c);
    body = err.empty() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + jsonStr(err) + "}";
    return true;
  }
  if (p == "/api/perf" && post) {
    // Swipe timing samples from the page: "one line per sample", appended to data\perf.log.
    if (q.body.size() < 256 * 1024) {
      std::string clean;
      for (char c : q.body)
        if (c == '\n' || c == '\t' || (unsigned char)c >= 0x20) clean += c;
      appendFileBytes(g_dataDir + L"\\perf.log", clean);
    }
    body = "{\"ok\":true}";
    return true;
  }
  if (p == "/api/rescan" && post) {
    coreStartScan();
    body = "{\"ok\":true}";
    return true;
  }
  if (p == "/api/history") {
    body = apiHistory(q.paramInt("id", -1), q.param("fp"));
    return true;
  }
  if (p == "/api/sort") {  // GET: counts per kind; POST: move them into subfolders (the page asks first)
    if (post) {
      const std::string err = coreSortStart(q.param("scope"), q.param("ids"), q.param("kinds"));
      body = err.empty() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + jsonStr(err) + "}";
    } else {
      body = coreSortCount(q.param("scope"), q.param("ids"), q.param("kinds", "passed,likeOnly,super"));
    }
    return true;
  }
  if (p == "/api/sort/undo" && post) {
    const std::string err = coreSortUndo();
    body = err.empty() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + jsonStr(err) + "}";
    return true;
  }
  if (p == "/api/sort/moves") {
    body = coreSortMoves();
    return true;
  }
  if (p == "/api/export") {
    // GET: progress of the current or last export; with what= also how many files it would take.
    // POST: start one (what, to, mode=copy|move). Files are moved only when mode says so.
    const std::string mode = q.param("mode", "copy");
    if (post) {
      const std::string err = mode == "copy" || mode == "move"
                                  ? coreExportStart(q.param("what"), q.param("to"), mode == "move")
                                  : std::string("書き出し方が正しくありません");
      body = err.empty() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + jsonStr(err) + "}";
    } else {
      body = coreExportState();
      if (q.params.count("what"))
        body.insert(body.size() - 1, ",\"count\":" + std::to_string(coreExportCount(q.param("what"), mode == "move")));
    }
    return true;
  }
  if (p == "/api/hiddenCount") {
    body = "{\"hidden\":" + std::to_string(coreHiddenBy(q.param("root"), q.paramInt("max_depth", -1))) + "}";
    return true;
  }
  if (p == "/api/qr") {  // a QR code for any short text (the phone link; also used by the tests)
    const auto rows = qrEncode(q.param("text"));
    body = "{\"size\":" + std::to_string(rows.size()) + ",\"rows\":[";
    for (size_t i = 0; i < rows.size(); ++i) body += (i ? ",\"" : "\"") + rows[i] + "\"";
    body += "]}";
    return true;
  }
  if (p == "/api/albums") {
    std::wstring file = g_dataDir + L"\\albums.json";
    if (post) {
      writeFileAtomic(file, q.body);
      body = "{\"ok\":true}";
    } else if (!readFileBytes(file, body) || body.empty()) {
      body = "[]";
    }
    return true;
  }
  return false;
}
