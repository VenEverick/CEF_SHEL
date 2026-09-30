#include "src/browser_app.h"

#include <filesystem>

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "src/platform.h"
#include "src/shell.h"

namespace shelter {

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty()) return;
#if defined(OS_MAC)
  // Без запроса доступа к Keychain при первом запуске.
  command_line->AppendSwitch("use-mock-keychain");
#endif
  // Приватный браузер: без фоновых служб Google.
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("no-default-browser-check");
  command_line->AppendSwitchWithValue("disable-features", "Translate");
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().Start();
}

void FillSettings(CefSettings& settings) {
  const std::string root = platform::UserDataDir();
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  CefString(&settings.root_cache_path) = root;
  CefString(&settings.log_file) = root + "/debug.log";
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.locale) = "ru";
  CefString(&settings.accept_language_list) = "ru-RU,ru,en-US,en";
  settings.no_sandbox = true;
}

}  // namespace shelter
