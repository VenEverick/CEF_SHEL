// SHELTER — платформенные хелперы (реализация: platform_win.cc / platform_mac.mm / platform_linux.cc).
#ifndef SHELTER_PLATFORM_H_
#define SHELTER_PLATFORM_H_

#include <array>
#include <string>
#include <vector>

namespace shelter {
namespace platform {

// Каталог с ресурсами UI (index.html, host-bridge.js, иконки).
std::string UiResourceDir();

// Каталог пользовательских данных (профили, кэш).
std::string UserDataDir();

// Каталог «Загрузки».
std::string DownloadsDir();

// Показать файл в Проводнике / Finder.
void ShowInFolder(const std::string& path);

// Открыть ссылку во внешнем приложении (mailto:, tel:, ...).
void OpenExternal(const std::string& url);

// Буфер обмена (текст, UTF-8).
std::string ClipboardRead();
bool ClipboardWrite(const std::string& text);

// Светлая/тёмная схема нативных элементов (macOS: NSAppearance).
void SetColorScheme(bool dark);

// Скругление углов и «дыры» (прозрачные участки) нативного вида вкладки.
// handle — CefBrowserHost::GetWindowHandle(); radii = {tl, tr, br, bl} и holes {x, y, w, h}
// задаются в DIP относительно вида вкладки (размер view_w x view_h DIP).
void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 4>>& holes, int view_w,
                   int view_h);

// Диагностика (CI): уровень предка окна, которому задаётся область (Windows), и дамп цепочки HWND.
void SetClipLevel(int level);
std::string DumpWindowChain(void* handle);

// macOS: первый клик по неактивному окну приложения не должен «съедаться».
void InstallInputFixes();

// Мастер-ключ секретов UI: 32 байта в hex (создаётся при первом запуске).
std::string SecretKeyHex();

// Диагностика (macOS): результат hitTest окон приложения в точке (x, y) окна, DIP от верхнего левого угла.
std::string DebugHitTest(double x, double y);

}  // namespace platform
}  // namespace shelter

#endif  // SHELTER_PLATFORM_H_
