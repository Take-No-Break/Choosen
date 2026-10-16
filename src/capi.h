// C interface of the core for apps that load it as a shared library (choosen_core.dll on Windows,
// libchoosen_core.so on Android): the Flutter app (Version 2) calls it through dart:ffi, and the iOS
// app (ios/) links it into the app.
// Strings are UTF-8. Returned strings are owned by the caller and released with choosen_free.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
#define CHOOSEN_EXTERN extern "C"
#else
#define CHOOSEN_EXTERN extern  // read as C (Swift's bridging header in ios/)
#endif
#if defined(_WIN32)
#define CHOOSEN_API CHOOSEN_EXTERN __declspec(dllexport)
#else
#define CHOOSEN_API CHOOSEN_EXTERN __attribute__((visibility("default")))
#endif

// Loads settings, the index and the rating journal from dataDir and starts scanning (once per
// process). Returns "" when the library is open, else the app that has dataDir open (Version 1 or
// another Version 2); then nothing is loaded and the call can be repeated later.
CHOOSEN_API char* choosen_init(const char* dataDir);
// One call of the page API (src/api_routes.cpp): returns "<status>\n<json>".
CHOOSEN_API char* choosen_api(const char* method, const char* path, const char* query, const char* body);
// Absolute path of an image ("" if unknown); fp is its fingerprint (16 hex digits) or "".
CHOOSEN_API char* choosen_item_path(int id, const char* fp);
// Changes whenever anything the screens show has changed.
CHOOSEN_API uint64_t choosen_version(void);
CHOOSEN_API void choosen_save_index(void);
CHOOSEN_API void choosen_daily_backup(void);
// Reads the photo folders again (the iOS app calls it when the Photos library changes).
CHOOSEN_API void choosen_rescan(void);
CHOOSEN_API void choosen_free(char* s);
