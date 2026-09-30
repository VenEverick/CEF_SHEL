#include "src/clients.h"

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include "src/common.h"
#include "src/shell.h"

namespace shelter {

namespace {

bool IsUiUrl(const std::string& url) {
  return url.rfind(kUiOriginPrefix, 0) == 0;
}

std::string HtmlEscape(const std::string& s) {
  std::string r;
  for (char c : s) {
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += c;
    }
  }
  return r;
}

std::string ErrorPageDataUrl(const std::string& url, const std::string& text) {
  const std::string html =
      "<!doctype html><html lang=ru><meta charset=utf-8>"
      "<meta name=color-scheme content='dark light'>"
      "<title>Не удалось открыть страницу</title><style>"
      "html,body{height:100%;margin:0}"
      "body{display:grid;place-items:center;background:#0E0F13;color:#EDEEF2;"
      "font:15px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
      "main{max-width:520px;padding:32px}h1{font-size:22px;margin:0 0 8px;font-weight:650}"
      "p{color:#A2A5AF;margin:6px 0}code{color:#EDEEF2;word-break:break-all}"
      "a{display:inline-block;margin-top:18px;padding:10px 18px;border-radius:12px;"
      "background:#1D1E25;color:#EDEEF2;text-decoration:none;border:1px solid #ffffff1a}"
      "a:hover{background:#252630}</style><main>"
      "<h1>Не удалось открыть страницу</h1>"
      "<p><code>" + HtmlEscape(url) + "</code></p>"
      "<p>" + HtmlEscape(text) + "</p>"
      "<a href=\"" + HtmlEscape(url) + "\">Повторить</a></main>";
  return "data:text/html;charset=utf-8;base64," +
         CefURIEncode(CefBase64Encode(html.data(), html.size()), false)
             .ToString();
}

}  // namespace

// ============================================================================
// UiClient
// ============================================================================

class UiClient::BridgeHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    CEF_REQUIRE_UI_THREAD();
    // Мост принимает вызовы только от UI-браузера и только со страницы UI.
    if (!Shell::Get().IsUiBrowser(browser) || !frame ||
        !IsUiUrl(frame->GetURL().ToString())) {
      callback->Failure(403, "forbidden");
      return true;
    }
    CefRefPtr<CefValue> v = ParseJson(request.ToString());
    if (!v || v->GetType() != VTYPE_DICTIONARY) {
      callback->Failure(400, "bad request");
      return true;
    }
    CefRefPtr<CefDictionaryValue> d = v->GetDictionary();
    const std::string method = d->GetString("m").ToString();
    CefRefPtr<CefDictionaryValue> args =
        d->HasKey("a") && d->GetType("a") == VTYPE_DICTIONARY
            ? d->GetDictionary("a")
            : CefDictionaryValue::Create();
    if (!Shell::Get().HandleBridge(browser, method, args, callback)) {
      callback->Failure(404, "unknown method: " + method);
    }
    return true;
  }
};

UiClient::UiClient() {
  CefMessageRouterConfig config;
  router_ = CefMessageRouterBrowserSide::Create(config);
  bridge_ = std::make_unique<BridgeHandler>();
  router_->AddHandler(bridge_.get(), false);
}

UiClient::~UiClient() {
  if (router_ && bridge_) router_->RemoveHandler(bridge_.get());
}

bool UiClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefProcessId source_process,
                                        CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

bool UiClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                             const CefString&, const CefString&,
                             cef_window_open_disposition_t, bool,
                             const CefPopupFeatures&, CefWindowInfo&,
                             CefRefPtr<CefClient>&, CefBrowserSettings&,
                             CefRefPtr<CefDictionaryValue>&, bool*) {
  return true;  // UI не открывает окна
}

void UiClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
  Shell::Get().OnUiCreated(browser);
}

void UiClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeClose(browser);
  Shell::Get().OnUiClosed(browser);
  Shell::Get().OnBrowserClosed();
}

bool UiClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              bool user_gesture,
                              bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeBrowse(browser, frame);
  // UI никогда не уходит со своей страницы (например, при drop ссылки на окно).
  if (frame->IsMain()) {
    const std::string url = request->GetURL().ToString();
    if (!IsUiUrl(url)) {
      return true;
    }
  }
  return false;
}

void UiClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                         TerminationStatus status, int,
                                         const CefString&) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnRenderProcessTerminated(browser);
  if (!Shell::Get().closing()) {
    browser->Reload();
  }
}

void UiClient::OnDraggableRegionsChanged(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame>,
    const std::vector<CefDraggableRegion>& regions) {
  CEF_REQUIRE_UI_THREAD();
  if (Shell::Get().IsUiBrowser(browser)) {
    Shell::Get().SetDraggableRegions(regions);
  }
}

void UiClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                   CefRefPtr<CefContextMenuParams>,
                                   CefRefPtr<CefMenuModel> model) {
  model->Clear();
}

bool UiClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                             const CefKeyEvent& event, CefEventHandle,
                             bool*) {
  if (event.type == KEYEVENT_RAWKEYDOWN && event.windows_key_code == 0x7B) {
    static const bool enabled =
        CefCommandLine::GetGlobalCommandLine()->HasSwitch("ui-devtools");
    if (enabled) {
      CefWindowInfo wi;
      CefBrowserSettings bs;
      browser->GetHost()->ShowDevTools(wi, nullptr, bs, CefPoint());
      return true;
    }
  }
  return false;
}

// ============================================================================
// TabClient
// ============================================================================

bool TabClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame>, int,
                              const CefString& target_url,
                              const CefString&,
                              cef_window_open_disposition_t disposition,
                              bool user_gesture, const CefPopupFeatures&,
                              CefWindowInfo&, CefRefPtr<CefClient>& client,
                              CefBrowserSettings&,
                              CefRefPtr<CefDictionaryValue>&, bool*) {
  CEF_REQUIRE_UI_THREAD();
  // window.open(..., 'features') по жесту пользователя — настоящее окно
  // (нужно для OAuth/оплат, где важен window.opener). Всё остальное — вкладка.
  if (disposition == CEF_WOD_NEW_POPUP && user_gesture) {
    client = new PopupClient();
    return false;
  }
  Shell::Get().OnTabNewWindow(browser, target_url.ToString());
  return true;
}

bool TabClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame>, const CefString& url,
                                 cef_window_open_disposition_t, bool) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabNewWindow(browser, url.ToString());
  return true;
}

void TabClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
  Shell::Get().OnTabCreated(browser, tab_id_);
}

void TabClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserClosed();
}

void TabClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                     bool isLoading, bool, bool) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabLoading(browser, isLoading);
}

void TabClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                            const CefString& errorText,
                            const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain() || errorCode == ERR_ABORTED) return;
  if (Tab* tab = Shell::Get().FindTabByBrowser(browser->GetIdentifier())) {
    tab->error_url = failedUrl.ToString();
  }
  frame->LoadURL(ErrorPageDataUrl(failedUrl.ToString(), errorText.ToString()));
}

void TabClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) return;
  Shell::Get().OnTabAddress(browser, url.ToString());
}

void TabClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                              const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabTitle(browser, title.ToString());
}

bool TabClient::RunContextMenu(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefContextMenuParams> params,
                               CefRefPtr<CefMenuModel>,
                               CefRefPtr<CefRunContextMenuCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  callback->Cancel();  // меню рисует UI (window.shelterCtxMenu)
  Shell::Get().OnTabContextMenu(browser, frame, params);
  return true;
}

bool TabClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabDownloadBefore(browser, item, suggested_name.ToString(),
                                   callback);
  return true;  // решение придёт из UI (dl.decision)
}

void TabClient::OnDownloadUpdated(CefRefPtr<CefBrowser>,
                                  CefRefPtr<CefDownloadItem> item,
                                  CefRefPtr<CefDownloadItemCallback>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabDownloadUpdated(item);
}

bool TabClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                              const CefKeyEvent& event, CefEventHandle,
                              bool*) {
  CEF_REQUIRE_UI_THREAD();
  if (event.type != KEYEVENT_RAWKEYDOWN) return false;
  return Shell::Get().OnTabKey(browser, event);
}

// ============================================================================
// PopupClient
// ============================================================================

void PopupClient::OnAfterCreated(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
}

void PopupClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserClosed();
}

void PopupClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (auto view = CefBrowserView::GetForBrowser(browser)) {
    if (auto window = view->GetWindow()) window->SetTitle(title);
  }
}

}  // namespace shelter
