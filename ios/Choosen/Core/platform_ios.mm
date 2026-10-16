// iOS implementation of platform.h and the file helpers of common.h.
//
// The app's own files (settings, index, rating journal, backups) are ordinary files in the app's
// Documents folder: they use the POSIX code shared with Android, included below under other names.
//
// The pictures come from the Photos library, which has no folders. It is shown to the core as a
// read-only tree under kPhotosRoot ("/photos"):
//   /photos/<album>/...            the user's albums, and folders of albums as subfolders
//   /photos/<no album>/...         the pictures that are in no album (the name follows the language)
// A picture is a small virtual file named "<date>_<uuid>.jpg" whose content is its Photos
// identifier. The core fingerprints a file by its size and bytes, so a picture keeps its identity
// (and its ratings) when albums are renamed, when it is in several albums, and across rescans.
// Nothing in the Photos library is ever changed here; writing to /photos fails.
#import <Foundation/Foundation.h>
#import <Photos/Photos.h>

#include "common.h"
#include "platform.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

// --- the shared POSIX code, renamed so the functions below can choose between it and Photos ---
#define isDirectory posixIsDirectory
#include "../../../android/app/src/main/cpp/common_posix.cpp"
#undef isDirectory

#define PlatFile PosixFile
#define platListDir posixListDir
#define platOpen posixOpen
#define platClose posixClose
#define platSize posixSize
#define platReadAt posixReadAt
#define platWriteAt posixWriteAt
#define platTruncate posixTruncate
#define platFlush posixFlush
#define platFileSize posixFileSize
#include "../../../android/app/src/main/cpp/platform_posix.cpp"
#undef PlatFile
#undef platListDir
#undef platOpen
#undef platClose
#undef platSize
#undef platReadAt
#undef platWriteAt
#undef platTruncate
#undef platFlush
#undef platFileSize

#include "photos_bridge.h"

namespace {

const std::string kPhotosRoot = "/photos";
const std::string kIdSuffix = "/L0/001";  // the usual tail of a local identifier after the UUID

std::mutex photosMu;
// "/photos/A/B" -> local identifier of that album or folder of albums, filled while listing
std::map<std::string, std::string> collectionByPath;
// uuid -> full local identifier of a picture, filled while listing (for unusual identifiers)
std::map<std::string, std::string> assetById;

bool underPhotos(const std::string& p) {
  return p == kPhotosRoot || (p.size() > kPhotosRoot.size() && p.compare(0, kPhotosRoot.size() + 1, kPhotosRoot + "/") == 0);
}

bool allowed() {
  const PHAuthorizationStatus s = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelReadWrite];
  return s == PHAuthorizationStatusAuthorized || s == PHAuthorizationStatusLimited;
}

std::string utf8(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }

// A collection's title as a folder name the core can use. The core was written for Windows names:
// it splits ranges on '|', reads a top name with ':' as another photo folder and joins paths with
// '\'. So the characters Windows forbids become their full-width forms (「a|b」 -> 「a｜b」), which
// read the same; a name that becomes equal to another one gets " (2)" in addCollections.
std::string folderName(NSString* title) {
  static const std::pair<char, const char*> wide[] = {{'/', "／"}, {'\\', "＼"}, {':', "："}, {'|', "｜"}, {'*', "＊"},
                                                       {'?', "？"}, {'"', "＂"}, {'<', "＜"}, {'>', "＞"}};
  std::string n;
  for (char c : utf8(title)) {
    const char* to = nullptr;
    for (const auto& w : wide)
      if (c == w.first) to = w.second;
    if (to) n += to;
    else if ((unsigned char)c >= 0x20) n += c;  // no control characters
  }
  while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
  return n.empty() ? "_" : n;
}

std::string noAlbumName() {
  NSString* lang = NSLocale.preferredLanguages.firstObject ?: @"en";
  if ([lang hasPrefix:@"ja"]) return "アルバムなし";
  if ([lang hasPrefix:@"pt"]) return "Sem álbum";
  return "No album";
}

PHFetchOptions* imageOptions() {
  PHFetchOptions* o = [PHFetchOptions new];
  o.predicate = [NSPredicate predicateWithFormat:@"mediaType == %d", (int)PHAssetMediaTypeImage];
  return o;
}

uint64_t mtimeOf(PHAsset* a) {
  NSDate* d = a.modificationDate ?: a.creationDate;
  return d ? (uint64_t)(d.timeIntervalSince1970 * 1e9) : 0;
}

std::string contentOf(const std::string& localId) { return "choosen-photo\n" + localId + "\n"; }

// "<yyyyMMdd_HHmmss>_<uuid>.jpg"; the uuid is the part of the local identifier before the first '/'.
std::string fileNameOf(PHAsset* a) {
  static NSDateFormatter* f = [] {
    NSDateFormatter* x = [NSDateFormatter new];
    x.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    x.dateFormat = @"yyyyMMdd_HHmmss";
    return x;
  }();
  const std::string id = utf8(a.localIdentifier);
  const std::string uuid = id.substr(0, id.find('/'));
  NSString* date = a.creationDate ? [f stringFromDate:a.creationDate] : @"00000000_000000";
  return utf8(date) + "_" + uuid + ".jpg";
}

void addAssets(PHFetchResult<PHAsset*>* r, std::vector<DirEntry>& out, const std::set<std::string>* skip) {
  for (PHAsset* a in r) {
    @autoreleasepool {
      const std::string id = utf8(a.localIdentifier);
      if (skip && skip->count(id)) continue;
      DirEntry e;
      e.name = widen(fileNameOf(a));
      e.size = (int64_t)contentOf(id).size();
      e.mtime = mtimeOf(a);
      out.push_back(std::move(e));
      const std::string uuid = id.substr(0, id.find('/'));
      if (id != uuid + kIdSuffix) {
        std::lock_guard<std::mutex> lk(photosMu);
        assetById[uuid] = id;
      }
    }
  }
}

// Child collections of a folder of albums (nil = the top level), as folder entries; also remembers
// their paths. Two collections with the same title get " (2)", " (3)" ….
void addCollections(PHFetchResult<PHCollection*>* r, const std::string& dir, std::vector<DirEntry>& out,
                    const std::string& reserved = "") {
  std::set<std::string> used;  // without letter case: data from Windows does not tell them apart
  auto lower = [](std::string x) {
    for (auto& ch : x) ch = (char)tolower((unsigned char)ch);
    return x;
  };
  if (!reserved.empty()) used.insert(lower(reserved));  // the no-album folder keeps its name
  for (PHCollection* c in r) {
    if (![c isKindOfClass:PHAssetCollection.class] && ![c isKindOfClass:PHCollectionList.class]) continue;
    std::string n = folderName(c.localizedTitle);
    for (int k = 2; used.count(lower(n)); ++k) n = folderName(c.localizedTitle) + " (" + std::to_string(k) + ")";
    used.insert(lower(n));
    DirEntry e;
    e.name = widen(n);
    e.dir = true;
    out.push_back(std::move(e));
    std::lock_guard<std::mutex> lk(photosMu);
    collectionByPath[dir + "/" + n] = utf8(c.localIdentifier);
  }
}

// Every picture that is in at least one of the user's albums.
std::set<std::string> inAnyAlbum() {
  std::set<std::string> ids;
  PHFetchResult<PHAssetCollection*>* albums =
      [PHAssetCollection fetchAssetCollectionsWithType:PHAssetCollectionTypeAlbum subtype:PHAssetCollectionSubtypeAny options:nil];
  for (PHAssetCollection* c in albums) {
    @autoreleasepool {
      for (PHAsset* a in [PHAsset fetchAssetsInAssetCollection:c options:imageOptions()]) ids.insert(utf8(a.localIdentifier));
    }
  }
  return ids;
}

bool listPhotos(const std::string& dir, std::vector<DirEntry>& out) {
  if (!allowed()) return false;
  @autoreleasepool {
    if (dir == kPhotosRoot) {
      addCollections([PHCollectionList fetchTopLevelUserCollectionsWithOptions:nil], dir, out, noAlbumName());
      DirEntry e;
      e.name = widen(noAlbumName());
      e.dir = true;
      out.push_back(std::move(e));
      return true;
    }
    if (dir == kPhotosRoot + "/" + noAlbumName()) {
      const std::set<std::string> skip = inAnyAlbum();
      addAssets([PHAsset fetchAssetsWithOptions:imageOptions()], out, &skip);
      return true;
    }
    std::string id;
    {
      std::lock_guard<std::mutex> lk(photosMu);
      auto it = collectionByPath.find(dir);
      if (it == collectionByPath.end()) return false;  // the core always lists a folder's parent first
      id = it->second;
    }
    PHAssetCollection* album = [PHAssetCollection fetchAssetCollectionsWithLocalIdentifiers:@[ @(id.c_str()) ] options:nil].firstObject;
    if (album) {
      addAssets([PHAsset fetchAssetsInAssetCollection:album options:imageOptions()], out, nullptr);
      return true;
    }
    PHCollectionList* list = [PHCollectionList fetchCollectionListsWithLocalIdentifiers:@[ @(id.c_str()) ] options:nil].firstObject;
    if (!list) return false;
    addCollections([PHCollection fetchCollectionsInCollectionList:list options:nil], dir, out);
    return true;
  }
}

// The local identifier of a picture file under /photos ("" if the name is not one of ours).
std::string assetIdOfPath(const std::string& p) {
  const size_t slash = p.rfind('/');
  std::string name = p.substr(slash == std::string::npos ? 0 : slash + 1);
  if (name.size() < 4 + 36 || name.compare(name.size() - 4, 4, ".jpg") != 0) return "";
  const std::string uuid = name.substr(name.size() - 4 - 36, 36);
  std::lock_guard<std::mutex> lk(photosMu);
  auto it = assetById.find(uuid);
  return it != assetById.end() ? it->second : uuid + kIdSuffix;
}

bool assetExists(const std::string& id) {
  if (id.empty() || !allowed()) return false;
  @autoreleasepool {
    return [PHAsset fetchAssetsWithLocalIdentifiers:@[ @(id.c_str()) ] options:nil].count > 0;
  }
}

}  // namespace

// --- common.h ---

bool isDirectory(const std::wstring& path) {
  const std::string p = posixPath(path);
  if (!underPhotos(p)) return posixIsDirectory(path);
  if (!allowed()) return false;
  if (p == kPhotosRoot || p == kPhotosRoot + "/" + noAlbumName()) return true;
  std::lock_guard<std::mutex> lk(photosMu);
  return collectionByPath.count(p) > 0;
}

// --- platform.h ---

bool platListDir(const std::wstring& dir, std::vector<DirEntry>& out) {
  const std::string p = posixPath(dir);
  return underPhotos(p) ? listPhotos(p, out) : posixListDir(dir, out);
}

struct PlatFile {
  PosixFile* real = nullptr;
  std::string mem;  // a picture under /photos: its identity record
};

PlatFile* platOpen(const std::wstring& path, PlatOpen mode) {
  const std::string p = posixPath(path);
  if (!underPhotos(p)) {
    PosixFile* f = posixOpen(path, mode);
    if (!f) return nullptr;
    auto* h = new PlatFile();
    h->real = f;
    return h;
  }
  if (mode != PlatOpen::Read) return nullptr;  // the Photos library is never written
  const std::string id = assetIdOfPath(p);
  if (!assetExists(id)) return nullptr;
  auto* h = new PlatFile();
  h->mem = contentOf(id);
  return h;
}

void platClose(PlatFile* f) {
  if (!f) return;
  if (f->real) posixClose(f->real);
  delete f;
}

int64_t platSize(PlatFile* f) { return f->real ? posixSize(f->real) : (int64_t)f->mem.size(); }

size_t platReadAt(PlatFile* f, int64_t off, void* buf, size_t n) {
  if (f->real) return posixReadAt(f->real, off, buf, n);
  if (off < 0 || off >= (int64_t)f->mem.size()) return 0;
  const size_t k = std::min(n, f->mem.size() - (size_t)off);
  memcpy(buf, f->mem.data() + off, k);
  return k;
}

bool platWriteAt(PlatFile* f, int64_t off, const std::string& data) { return f->real && posixWriteAt(f->real, off, data); }
bool platTruncate(PlatFile* f, int64_t size) { return f->real && posixTruncate(f->real, size); }
bool platFlush(PlatFile* f) { return f->real && posixFlush(f->real); }

int64_t platFileSize(const std::wstring& path) {
  const std::string p = posixPath(path);
  if (!underPhotos(p)) return posixFileSize(path);
  const std::string id = assetIdOfPath(p);
  return assetExists(id) ? (int64_t)contentOf(id).size() : -1;
}

// --- for the app (photos_bridge.h) ---

std::string choosenPhotosAssetId(const std::string& path) { return underPhotos(path) ? assetIdOfPath(path) : ""; }
std::string choosenPhotosRoot() { return kPhotosRoot; }

@implementation ChoosenPhotos
+ (NSString*)assetIdForPath:(NSString*)path {
  const std::string id = choosenPhotosAssetId(path.UTF8String);
  return id.empty() ? nil : @(id.c_str());
}
+ (NSString*)root {
  return @(choosenPhotosRoot().c_str());
}
@end
