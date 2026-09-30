#include "src/renderer_app.h"

#include <string>

#include "src/common.h"

namespace shelter {

void AppBase::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(
      kUiScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                     CEF_SCHEME_OPTION_CORS_ENABLED |
                     CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void AppBase::OnWebKitInitialized() {
  CefMessageRouterConfig config;  // window.cefQuery / window.cefQueryCancel
  router_ = CefMessageRouterRendererSide::Create(config);
}

namespace {

// window.cefQuery выдаём ТОЛЬКО chrome-UI. Обычные сайты доступа к мосту не получают.
bool IsUiFrame(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsMain()) return false;
  const std::string url = frame->GetURL().ToString();
  return url.rfind(kUiOriginPrefix, 0) == 0;
}

}  // namespace

void AppBase::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (router_ && IsUiFrame(frame)) {
    router_->OnContextCreated(browser, frame, context);
  }
}

void AppBase::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  if (router_ && IsUiFrame(frame)) {
    router_->OnContextReleased(browser, frame, context);
  }
}

bool AppBase::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefProcessId source_process,
                                       CefRefPtr<CefProcessMessage> message) {
  return router_ &&
         router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

}  // namespace shelter
