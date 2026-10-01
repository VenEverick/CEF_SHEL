#include "src/platform.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <string>

namespace shelter {
namespace platform {

namespace {

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr,
                      nullptr);
  return s;
}

std::wstring Wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) {
    out = Utf8(p);
    CoTaskMemFree(p);
  }
  return out;
}

}  // namespace

std::string UiResourceDir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  std::wstring path(buf, n);
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) path.resize(slash);
  return Utf8(path) + "\\ui";
}

std::string UserDataDir() {
  std::string base = KnownFolder(FOLDERID_LocalAppData);
  if (base.empty()) base = ".";
  return base + "\\SHELTER";
}

std::string DownloadsDir() {
  std::string d = KnownFolder(FOLDERID_Downloads);
  return d.empty() ? UserDataDir() + "\\Downloads" : d;
}

void ShowInFolder(const std::string& path) {
  std::wstring args = L"/select,\"" + Wide(path) + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                SW_SHOWNORMAL);
}

void OpenExternal(const std::string& url) {
  ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

std::string ClipboardRead() {
  std::string out;
  if (!OpenClipboard(nullptr)) return out;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (p) {
      out = Utf8(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

bool ClipboardWrite(const std::string& text) {
  std::wstring w = Wide(text);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (!mem) return false;
  void* dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    return false;
  }
  memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(mem);
  if (!OpenClipboard(nullptr)) {
    GlobalFree(mem);
    return false;
  }
  EmptyClipboard();
  bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
  if (!ok) GlobalFree(mem);
  CloseClipboard();
  return ok;
}

void SetColorScheme(bool /*dark*/) {}

std::string DebugHitTest(double, double) { return std::string(); }

static int g_clip_level = -1;

void SetClipLevel(int level) { g_clip_level = level; }

std::string DumpWindowChain(void* handle) {
  HWND h = static_cast<HWND>(handle);
  std::string out;
  HWND root = h ? GetAncestor(h, GA_ROOT) : nullptr;
  for (HWND c = h; c; c = GetParent(c)) {
    wchar_t cls[128] = {0};
    GetClassNameW(c, cls, 127);
    RECT r{};
    GetWindowRect(c, &r);
    char buf[512];
    char cls8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls8, 255, nullptr, nullptr);
    RECT rb{};
    int rt = GetWindowRgnBox(c, &rb);
    snprintf(buf, sizeof(buf), "  hwnd=%p cls=%s rect=%ld,%ld,%ld,%ld style=%08lx ex=%08lx rgn=%d\n", (void*)c, cls8,
             r.left, r.top, r.right - r.left, r.bottom - r.top,
             static_cast<unsigned long>(GetWindowLongPtrW(c, GWL_STYLE)),
             static_cast<unsigned long>(GetWindowLongPtrW(c, GWL_EXSTYLE)), rt);
    out += buf;
    if (c == root) break;
  }
  if (root) {
    out += " root children:\n";
    for (HWND c = GetWindow(root, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
      wchar_t cls[128] = {0};
      GetClassNameW(c, cls, 127);
      char cls8[256] = {0};
      WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls8, 255, nullptr, nullptr);
      RECT r{};
      GetWindowRect(c, &r);
      char buf[512];
      snprintf(buf, sizeof(buf), "  child hwnd=%p cls=%s rect=%ld,%ld,%ld,%ld vis=%d\n", (void*)c, cls8, r.left,
               r.top, r.right - r.left, r.bottom - r.top, IsWindowVisible(c) ? 1 : 0);
      out += buf;
    }
  }
  return out;
}

void InstallInputFixes() {}

void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 4>>& holes, int view_w,
                   int view_h) {
  HWND h = static_cast<HWND>(handle);
  if (!h || !IsWindow(h) || view_w <= 0 || view_h <= 0) return;
  // Область нужно задать окну — прямому потомку главного окна (виджет overlay).
  HWND root = GetAncestor(h, GA_ROOT);
  HWND cur = h;
  if (g_clip_level < 0) {
    while (cur && GetParent(cur) && GetParent(cur) != root) cur = GetParent(cur);
  } else {
    for (int i = 0; i < g_clip_level && cur && GetParent(cur) && GetParent(cur) != root; ++i) cur = GetParent(cur);
  }
  if (!cur) return;

  bool any = !holes.empty();
  for (int i = 0; i < 4; ++i) any = any || radii[i] > 0.5;
  if (!any) {
    SetWindowRgn(cur, nullptr, TRUE);
    return;
  }
  RECT rc;
  if (!GetWindowRect(cur, &rc)) return;
  const int W = rc.right - rc.left, H = rc.bottom - rc.top;
  if (W <= 0 || H <= 0) return;
  const double sx = static_cast<double>(W) / view_w;
  const double sy = static_cast<double>(H) / view_h;

  HRGN rgn = CreateRectRgn(0, 0, W, H);
  // углы: tl, tr, br, bl
  for (int i = 0; i < 4; ++i) {
    const int r = static_cast<int>(radii[i] * sx + 0.5);
    if (r <= 0) continue;
    int x0 = 0, y0 = 0, ex0 = 0, ey0 = 0;
    switch (i) {
      case 0: x0 = 0;     y0 = 0;     ex0 = 0;         ey0 = 0;         break;
      case 1: x0 = W - r; y0 = 0;     ex0 = W - 2 * r; ey0 = 0;         break;
      case 2: x0 = W - r; y0 = H - r; ex0 = W - 2 * r; ey0 = H - 2 * r; break;
      default: x0 = 0;    y0 = H - r; ex0 = 0;         ey0 = H - 2 * r; break;
    }
    HRGN cut = CreateRectRgn(x0, y0, x0 + r, y0 + r);
    HRGN ell = CreateEllipticRgn(ex0, ey0, ex0 + 2 * r + 1, ey0 + 2 * r + 1);
    CombineRgn(cut, cut, ell, RGN_DIFF);
    CombineRgn(rgn, rgn, cut, RGN_DIFF);
    DeleteObject(cut);
    DeleteObject(ell);
  }
  for (const auto& hl : holes) {
    HRGN hr = CreateRectRgn(static_cast<int>(hl[0] * sx), static_cast<int>(hl[1] * sy),
                            static_cast<int>((hl[0] + hl[2]) * sx + 0.5),
                            static_cast<int>((hl[1] + hl[3]) * sy + 0.5));
    CombineRgn(rgn, rgn, hr, RGN_DIFF);
    DeleteObject(hr);
  }
  if (!SetWindowRgn(cur, rgn, TRUE)) DeleteObject(rgn);  // при успехе регионом владеет система
}

}  // namespace platform
}  // namespace shelter
