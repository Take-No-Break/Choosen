// Thumbnails via the Windows shell (same cache Explorer uses — no copies of
// the photos are written by Choosen), re-encoded to JPEG with WIC.
#pragma once
#include "common.h"

// Returns a JPEG of the file scaled to fit size x size. Cached in memory.
bool shellJpeg(const std::wstring& path, uint64_t key, int size, std::string& out);
// Opens an Explorer window with the file selected.
void revealInExplorer(const std::wstring& path);
// Shows the native folder picker. Returns empty string when cancelled.
std::wstring pickFolder(const std::wstring& initial);
