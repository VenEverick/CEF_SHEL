// SHELTER — точка входа Windows (браузерный и все вспомогательные процессы).
#include <windows.h>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "src/browser_app.h"
#include "src/renderer_app.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
  CefMainArgs main_args(hInstance);

  CefRefPtr<CefCommandLine> cl = CefCommandLine::CreateCommandLine();
  cl->InitFromString(::GetCommandLineW());
  const bool is_browser = cl->GetSwitchValue("type").empty();

  CefRefPtr<CefApp> app;
  if (is_browser) {
    app = new shelter::BrowserApp();
  } else {
    app = new shelter::RendererApp();
  }

  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  shelter::FillSettings(settings);

  if (!CefInitialize(main_args, settings, app, nullptr)) {
    return CefGetExitCode();
  }
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
