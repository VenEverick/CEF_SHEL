// SHELTER — платформенные хелперы (реализация: platform_win.cc / platform_mac.mm / platform_linux.cc).
#ifndef SHELTER_PLATFORM_H_
#define SHELTER_PLATFORM_H_

#include <string>

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

// Диагностика (macOS): результат hitTest окон приложения в точке (x, y) окна, DIP от верхнего левого угла.
std::string DebugHitTest(double x, double y);

}  // namespace platform
}  // namespace shelter

#endif  // SHELTER_PLATFORM_H_
