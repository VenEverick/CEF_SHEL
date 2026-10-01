#include "src/platform.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <cwchar>
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

void ApplyViewClip(void*, const double[4], const std::vector<std::array<int, 4>>&, int, int) {
  // Windows: содержимое вкладки рисует общая поверхность DirectComposition, область HWND на
  // пиксели не влияет. Скругление/«дыры» не используются (см. USE_HOLES в host-bridge.js).
}

}  // namespace platform
}  // namespace shelter
