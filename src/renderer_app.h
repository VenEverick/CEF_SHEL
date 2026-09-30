// SHELTER — CefApp, общий для всех процессов (схема shelter:// + renderer-часть моста).
#ifndef SHELTER_RENDERER_APP_H_
#define SHELTER_RENDERER_APP_H_

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"

namespace shelter {

// Базовая часть: регистрация схемы и renderer-обработчик.
// Не содержит IMPLEMENT_REFCOUNTING — его ставит конечный класс.
class AppBase : public CefApp, public CefRenderProcessHandler {
 public:
  AppBase() = default;

  // CefApp
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefRenderProcessHandler
  void OnWebKitInitialized() override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  CefRefPtr<CefMessageRouterRendererSide> router_;
};

// Используется в вспомогательных процессах (renderer/gpu/...) и на Windows.
class RendererApp final : public AppBase {
 public:
  RendererApp() = default;

 private:
  IMPLEMENT_REFCOUNTING(RendererApp);
  DISALLOW_COPY_AND_ASSIGN(RendererApp);
};

}  // namespace shelter

#endif  // SHELTER_RENDERER_APP_H_
