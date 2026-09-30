// SHELTER — CefApp главного (browser) процесса.
#ifndef SHELTER_BROWSER_APP_H_
#define SHELTER_BROWSER_APP_H_

#include "include/cef_app.h"
#include "src/renderer_app.h"

namespace shelter {

class BrowserApp final : public AppBase, public CefBrowserProcessHandler {
 public:
  BrowserApp() = default;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;

  // CefBrowserProcessHandler
  void OnContextInitialized() override;

 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

// Общие настройки CefSettings (пути, язык, логи).
void FillSettings(CefSettings& settings);

}  // namespace shelter

#endif  // SHELTER_BROWSER_APP_H_
