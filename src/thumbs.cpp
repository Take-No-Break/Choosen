#include "win.h"

#include "thumbs.h"

#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <list>
#include <mutex>
#include <unordered_map>

namespace {

// Small LRU of encoded JPEGs so scrolling back through the gallery is free.
struct Lru {
  std::mutex mu;
  std::list<std::pair<uint64_t, std::string>> order;  // front = newest
  std::unordered_map<uint64_t, std::list<std::pair<uint64_t, std::string>>::iterator> map;
  size_t bytes = 0;
  const size_t cap = 256u << 20;

  bool get(uint64_t k, std::string& out) {
    std::lock_guard<std::mutex> lk(mu);
    auto it = map.find(k);
    if (it == map.end()) return false;
    order.splice(order.begin(), order, it->second);
    out = it->second->second;
    return true;
  }
  void put(uint64_t k, const std::string& v) {
    std::lock_guard<std::mutex> lk(mu);
    if (map.count(k)) return;
    order.emplace_front(k, v);
    map[k] = order.begin();
    bytes += v.size();
    while (bytes > cap && !order.empty()) {
      bytes -= order.back().second.size();
      map.erase(order.back().first);
      order.pop_back();
    }
  }
} g_lru;

template <class T>
struct Com {
  T* p = nullptr;
  ~Com() {
    if (p) p->Release();
  }
  T** operator&() { return &p; }
  T* operator->() { return p; }
};

thread_local IWICImagingFactory* t_wic = nullptr;  // one per connection thread

IWICImagingFactory* wic() {
  if (!t_wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t_wic));
  return t_wic;
}

bool encodeJpeg(const uint8_t* bgr, int w, int h, int stride, std::string& out) {
  IWICImagingFactory* f = wic();
  if (!f) return false;
  Com<IStream> st;
  st.p = SHCreateMemStream(nullptr, 0);
  if (!st.p) return false;
  Com<IWICBitmapEncoder> enc;
  if (FAILED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc))) return false;
  if (FAILED(enc->Initialize(st.p, WICBitmapEncoderNoCache))) return false;
  Com<IWICBitmapFrameEncode> fr;
  Com<IPropertyBag2> props;
  if (FAILED(enc->CreateNewFrame(&fr, &props))) return false;
  PROPBAG2 opt{};
  opt.pstrName = (LPOLESTR)L"ImageQuality";
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_R4;
  v.fltVal = 0.88f;
  props->Write(1, &opt, &v);
  if (FAILED(fr->Initialize(props.p))) return false;
  fr->SetSize(w, h);
  WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
  fr->SetPixelFormat(&fmt);
  if (FAILED(fr->WritePixels(h, stride, stride * h, (BYTE*)bgr))) return false;
  if (FAILED(fr->Commit()) || FAILED(enc->Commit())) return false;
  STATSTG ss{};
  st->Stat(&ss, STATFLAG_NONAME);
  out.resize((size_t)ss.cbSize.QuadPart);
  LARGE_INTEGER zero{};
  st->Seek(zero, STREAM_SEEK_SET, nullptr);
  ULONG got = 0;
  st->Read(out.data(), (ULONG)out.size(), &got);
  out.resize(got);
  return got > 0;
}

}  // namespace

void thumbsThreadCleanup() {
  if (t_wic) {
    t_wic->Release();
    t_wic = nullptr;
  }
}

bool shellJpeg(const std::wstring& path, uint64_t key, int size, std::string& out) {
  uint64_t k = key ^ ((uint64_t)size * 0x9E3779B97F4A7C15ULL);
  if (g_lru.get(k, out)) return true;

  Com<IShellItemImageFactory> fac;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&fac)))) return false;
  HBITMAP hbm = nullptr;
  SIZE sz{size, size};
  if (FAILED(fac->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY, &hbm)) || !hbm) return false;

  BITMAP bm{};
  GetObject(hbm, sizeof bm, &bm);
  int w = bm.bmWidth, h = bm.bmHeight;
  if (w <= 0 || h <= 0) {
    DeleteObject(hbm);
    return false;
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  std::vector<uint8_t> px((size_t)w * h * 4);
  HDC dc = GetDC(nullptr);
  int rows = GetDIBits(dc, hbm, 0, h, px.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  DeleteObject(hbm);
  if (rows != h) return false;

  // Transparent images come back premultiplied; flatten onto the app's dark background.
  bool allZero = true, partial = false;
  for (size_t i = 3; i < px.size(); i += 4) {
    if (px[i] != 0) allZero = false;
    if (px[i] != 255) partial = true;
  }
  const bool blend = !allZero && partial;
  const int bg = 0x1c;
  int stride = (w * 3 + 3) & ~3;
  std::vector<uint8_t> bgr((size_t)stride * h);
  for (int y = 0; y < h; ++y) {
    const uint8_t* s = &px[(size_t)y * w * 4];
    uint8_t* d = &bgr[(size_t)y * stride];
    for (int x = 0; x < w; ++x, s += 4, d += 3) {
      if (blend) {
        int inv = 255 - s[3];
        d[0] = (uint8_t)std::min(255, s[0] + bg * inv / 255);
        d[1] = (uint8_t)std::min(255, s[1] + bg * inv / 255);
        d[2] = (uint8_t)std::min(255, s[2] + bg * inv / 255);
      } else {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
      }
    }
  }
  if (!encodeJpeg(bgr.data(), w, h, stride, out)) return false;
  g_lru.put(k, out);
  return true;
}

void revealInExplorer(const std::wstring& path) {
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
  if (!pidl) return;
  SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
  ILFree(pidl);
}

std::wstring pickFolder(const std::wstring& initial) {
  std::wstring result;
  Com<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
    return result;
  FILEOPENDIALOGOPTIONS opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(L"整理する写真フォルダを選択");
  if (!initial.empty()) {
    Com<IShellItem> start;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start))))
      dlg->SetFolder(start.p);
  }
  if (FAILED(dlg->Show(GetForegroundWindow()))) return result;
  Com<IShellItem> item;
  if (FAILED(dlg->GetResult(&item))) return result;
  PWSTR p = nullptr;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
    result = p;
    CoTaskMemFree(p);
  }
  return result;
}
