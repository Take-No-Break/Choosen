// The bridge between the Android app (com.choosen.app.Core) and the shared C++ core.
// Strings cross as UTF-16 (Java) <-> UTF-8 (core), so file names with emoji survive.
#include <jni.h>

#include "api_routes.h"
#include "common.h"
#include "core.h"

#include <mutex>

namespace {

std::string toUtf8(JNIEnv* env, jstring js) {
  if (!js) return "";
  const jsize n = env->GetStringLength(js);
  std::u16string u(n, u'\0');
  env->GetStringRegion(js, 0, n, reinterpret_cast<jchar*>(u.data()));
  std::wstring w;
  for (size_t i = 0; i < u.size(); ++i) {
    uint32_t c = u[i];
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < u.size()) c = 0x10000 + ((c - 0xd800) << 10) + (u[++i] - 0xdc00);
    w += (wchar_t)c;
  }
  return narrow(w);
}

jstring toJava(JNIEnv* env, const std::string& s) {
  std::wstring w = widen(s);
  std::u16string u;
  u.reserve(w.size());
  for (wchar_t wc : w) {
    uint32_t c = (uint32_t)wc;
    if (c >= 0x10000) {
      c -= 0x10000;
      u += (char16_t)(0xd800 + (c >> 10));
      u += (char16_t)(0xdc00 + (c & 0x3ff));
    } else {
      u += (char16_t)c;
    }
  }
  return env->NewString(reinterpret_cast<const jchar*>(u.data()), (jsize)u.size());
}

std::once_flag started;

}  // namespace

extern "C" {

// Loads the settings, the index and the rating journal from dataDir and starts the scan.
JNIEXPORT void JNICALL Java_com_choosen_app_Core_init(JNIEnv* env, jclass, jstring dataDir) {
  const std::string dir = toUtf8(env, dataDir);
  std::call_once(started, [&] {
    g_baseDir = g_dataDir = widen(dir);
    loadConfig();
    coreInit();
    coreStartScan();
  });
}

// One API call of the page. Returns "<status>\n<json>".
JNIEXPORT jstring JNICALL Java_com_choosen_app_Core_api(JNIEnv* env, jclass, jstring method, jstring path,
                                                        jstring query, jstring body) {
  ApiCall q;
  q.method = toUtf8(env, method);
  q.path = toUtf8(env, path);
  q.body = toUtf8(env, body);
  parseQueryInto(toUtf8(env, query), q.params);
  int status = 200;
  std::string out;
  if (!apiRoute(q, status, out)) {
    status = 404;
    out = "{\"error\":\"not found\"}";
  }
  return toJava(env, std::to_string(status) + "\n" + out);
}

// Absolute path of an image ("" if unknown), for /img/ and /thumb/.
JNIEXPORT jstring JNICALL Java_com_choosen_app_Core_itemPath(JNIEnv* env, jclass, jint id, jstring fp) {
  std::wstring path;
  uint64_t f;
  std::string ext;
  if (!itemFile(id, toUtf8(env, fp), path, f, ext)) return toJava(env, "");
  std::string p = narrow(path);
  for (auto& c : p)
    if (c == '\\') c = '/';
  return toJava(env, p);
}

JNIEXPORT jlong JNICALL Java_com_choosen_app_Core_version(JNIEnv*, jclass) { return (jlong)coreVersion(); }

JNIEXPORT void JNICALL Java_com_choosen_app_Core_saveIndex(JNIEnv*, jclass) { coreSaveIndex(); }

JNIEXPORT void JNICALL Java_com_choosen_app_Core_dailyBackup(JNIEnv*, jclass) { coreDailyBackup(); }

JNIEXPORT void JNICALL Java_com_choosen_app_Core_rescan(JNIEnv*, jclass) { coreStartScan(); }

}  // extern "C"
