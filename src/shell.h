// SHELTER — ядро оболочки: одно окно (CEF Views), chrome-UI и вкладки.
//
//  ┌─ CefWindow (frameless) ────────────────────────────────────────────┐
//  │  CefBrowserView #0  — chrome-UI (shelter://app/index.html)         │
//  │  ┌────────────────────────────────────────────────────────────┐   │
//  │  │ дочерний вид: CefBrowserView вкладки (по одной на вкладку), │   │
//  │  │ накладывается на прямоугольник #viewport из вёрстки         │   │
//  │  └────────────────────────────────────────────────────────────┘   │
//  └────────────────────────────────────────────────────────────────────┘
//
// Вкладки = отдельные CefBrowser с собственным CefRequestContext на
// «пространство» (persist:space-<key>) либо in-memory контекстом для режима
// «Призрак» (temp:ghost-<key>). Всё состояние вкладок (история, заголовки,
// пространства) остаётся в UI, оболочка только показывает страницы и шлёт события.
#ifndef SHELTER_SHELL_H_
#define SHELTER_SHELL_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_message_router.h"

namespace shelter {

class TabClient;
class UiClient;

struct Tab {
  std::string id;
  std::string partition;
  std::string requested_url;  // последний URL, который попросил UI
  std::string current_url;    // текущий адрес в браузере вкладки
  std::string error_url;      // URL, не открывшийся (показана страница ошибки)
  bool loading = false;
  int browser_id = 0;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<TabClient> client;
  std::string pending_url;  // если браузер ещё не создан
  CefRefPtr<CefDevToolsMessageObserver> snap_observer;
  CefRefPtr<CefRegistration> snap_registration;
};

struct PendingDownload {
  CefRefPtr<CefBeforeDownloadCallback> callback;
  std::string filename;
};

class Shell {
 public:
  static Shell& Get();

  // ---- жизненный цикл ----
  void Start();                 // UI-поток, после OnContextInitialized
  void RequestClose();          // Cmd+Q / закрытие из UI
  void OnBrowserCreated();      // счётчик живых браузеров (все клиенты)
  void OnBrowserClosed();
  bool closing() const { return closing_; }

  // ---- окно ----
  bool CanCloseWindow();
  void OnWindowCreated(CefRefPtr<CefWindow> window);
  void OnWindowDestroyed();
  void OnRootLayout(const CefRect& bounds);
  void TrackPopupWindow(CefRefPtr<CefWindow> window);
  void UntrackPopupWindow(CefRefPtr<CefWindow> window);
  CefRefPtr<CefWindow> window() const { return window_; }

  // ---- UI-браузер ----
  CefRefPtr<CefBrowserView> CreateUiView();
  void OnUiCreated(CefRefPtr<CefBrowser> browser);
  void OnUiClosed(CefRefPtr<CefBrowser> browser);
  bool IsUiBrowser(CefRefPtr<CefBrowser> browser) const;
  void SetDraggableRegions(const std::vector<CefDraggableRegion>& regions);
  // Вызвать window.__shelterHost.ev(name, payload).
  void UiEvent(const std::string& name, const std::string& payload_json);

  // ---- вызовы моста (UI -> native) ----
  // Возвращает true, если метод известен (и callback будет вызван).
  bool HandleBridge(CefRefPtr<CefBrowser> browser,
                    const std::string& method,
                    CefRefPtr<CefDictionaryValue> args,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  // ---- события вкладок (TabClient -> Shell) ----
  Tab* FindTab(const std::string& id);
  Tab* FindTabByBrowser(int browser_id);
  void OnTabCreated(CefRefPtr<CefBrowser> browser, const std::string& id);
  void OnTabAddress(CefRefPtr<CefBrowser> browser, const std::string& url);
  void OnTabTitle(CefRefPtr<CefBrowser> browser, const std::string& title);
  void OnTabLoading(CefRefPtr<CefBrowser> browser, bool loading);
  void OnTabNewWindow(CefRefPtr<CefBrowser> browser, const std::string& url);
  bool OnTabKey(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event);
  void OnTabContextMenu(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefContextMenuParams> params);
  void OnTabDownloadBefore(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefDownloadItem> item,
                           const std::string& suggested_name,
                           CefRefPtr<CefBeforeDownloadCallback> callback);
  void OnTabDownloadUpdated(CefRefPtr<CefDownloadItem> item);

 private:
  Shell() = default;

  void MaybeQuit();

  // контексты (сессии)
  CefRefPtr<CefRequestContext> ContextFor(const std::string& partition);
  void ReleaseContextIfUnused(const std::string& partition);
  void ApplyPendingWipes();
  void QueueWipe(const std::string& partition);

  // вкладки
  Tab* CreateTab(const std::string& id,
                 const std::string& partition,
                 const std::string& url,
                 double zoom);
  void DestroyTab(const std::string& id);
  void HideAllTabs();
  void LayoutTab(Tab* tab, const CefRect& rect, bool visible);
  CefRefPtr<CefBrowser> ActiveBrowser();
  void CaptureSnapshot(const std::string& tab_id, int quality,
                       CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);
  void SetZoomAll(double factor);
  void ClearPrivacy(CefRefPtr<CefListValue> parts,
                    CefRefPtr<CefDictionaryValue> opts,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);
  void TabAction(const std::string& id,
                 const std::string& act,
                 const std::string& url);
  void DownloadDecision(const std::string& id, const std::string& action);

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_;  // контейнер: UI-вид + виды вкладок (без overlay-окон)
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowser> ui_browser_;
  std::vector<CefDraggableRegion> drag_regions_;
  std::set<CefRefPtr<CefWindow>> popup_windows_;

  std::map<std::string, Tab> tabs_;  // id вкладки UI -> Tab
  std::string active_tab_;
  CefRect last_rect_;
  double zoom_ = 1.0;
  std::map<std::string, CefRefPtr<CefRequestContext>> contexts_;
  std::map<std::string, PendingDownload> pending_downloads_;
  std::string last_ctx_tab_;

  int browser_count_ = 0;
  bool closing_ = false;
  bool quit_posted_ = false;
};

// Сравнение адресов без учёта схемы, www, завершающего '/' и регистра.
bool SameUrl(const std::string& a, const std::string& b);

// Общий делегат BrowserView: Alloy-стиль + всплывающие окна (popup/DevTools).
class ShellBrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  ShellBrowserViewDelegate() = default;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  IMPLEMENT_REFCOUNTING(ShellBrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(ShellBrowserViewDelegate);
};

}  // namespace shelter

#endif  // SHELTER_SHELL_H_
