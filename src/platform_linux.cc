// Только для локальной проверки синтаксиса в Linux-песочнице. Не входит в релизные сборки.
#include "src/platform.h"

#include <cstdlib>

namespace shelter {
namespace platform {
std::string UiResourceDir() { return "./ui"; }
std::string UserDataDir() {
  const char* h = std::getenv("HOME");
  return std::string(h ? h : ".") + "/.config/SHELTER";
}
std::string DownloadsDir() {
  const char* h = std::getenv("HOME");
  return std::string(h ? h : ".") + "/Downloads";
}
void ShowInFolder(const std::string&) {}
void OpenExternal(const std::string&) {}
void SetColorScheme(bool) {}
}  // namespace platform
}  // namespace shelter
namespace shelter {
namespace platform {
std::string ClipboardRead() { return std::string(); }
bool ClipboardWrite(const std::string&) { return false; }
std::string DebugHitTest(double, double) { return std::string(); }
void ApplyViewClip(void*, const double[4], const std::vector<std::array<int, 4>>&, int, int) {}
void SetClipLevel(int) {}
std::string DumpWindowChain(void*) { return std::string(); }

void InstallInputFixes() {}
}  // namespace platform
}  // namespace shelter
