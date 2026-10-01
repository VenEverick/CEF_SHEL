#include "src/shell.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "src/clients.h"
#include "src/common.h"
#include "src/key_map.h"
#include "src/platform.h"
#include "src/ui_scheme.h"

namespace fs = std::filesystem;

namespace shelter {

namespace {

constexpr int kMinWidth = 960;
constexpr int kMinHeight = 620;

CefRefPtr<CefImage> LoadWindowIcon() {
  std::ifstream f(platform::UiResourceDir() + "/icon-256.png", std::ios::binary);
  if (!f) return nullptr;
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string data = ss.str();
  CefRefPtr<CefImage> img = CefImage::CreateImage();
  if (!img->AddPNG(1.0f, data.data(), data.size())) return nullptr;
  return img;
}

// ---- окно ------------------------------------------------------------------

class ShellWindowDelegate : public CefWindowDelegate {
 public:
  explicit ShellWindowDelegate(CefRefPtr<CefBrowserView> ui_view)
      : ui_view_(ui_view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    Shell::Get().OnWindowCreated(window);
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    ui_view_ = nullptr;
    Shell::Get().OnWindowDestroyed();
  }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    return Shell::Get().CanCloseWindow();
  }

  // Окно без системной рамки: заголовок, кнопки и перетаскивание — в вёрстке.
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  // macOS: оставляем нативные «светофоры» поверх вёрстки.
  bool WithStandardWindowButtons(CefRefPtr<CefWindow>) override { return true; }
  bool GetTitlebarHeight(CefRefPtr<CefWindow>, float* height) override {
    *height = 48.f;  // центр кнопок по вертикали = 24 px (как .sb-top в вёрстке)
    return true;
  }
  bool CanResize(CefRefPtr<CefWindow>) override { return true; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return true; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return true; }

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    int w = 1440, h = 900;
    if (auto d = CefDisplay::GetPrimaryDisplay()) {
      CefRect wa = d->GetWorkArea();
      w = std::min(w, std::max(kMinWidth, wa.width - 80));
      h = std::min(h, std::max(kMinHeight, wa.height - 80));
    }
    return CefSize(w, h);
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(kMinWidth, kMinHeight);
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  CefRefPtr<CefBrowserView> ui_view_;
  IMPLEMENT_REFCOUNTING(ShellWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(ShellWindowDelegate);
};

// Окно для popup'ов страниц и DevTools.
class PopupWindowDelegate : public CefWindowDelegate {
 public:
  explicit PopupWindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(view_);
    window->Show();
    Shell::Get().TrackPopupWindow(window);
    view_->RequestFocus();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    Shell::Get().UntrackPopupWindow(window);
    view_ = nullptr;
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    CefRefPtr<CefBrowser> b = view_ ? view_->GetBrowser() : nullptr;
    return b ? b->GetHost()->TryCloseBrowser() : true;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(1000, 720);
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(PopupWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(PopupWindowDelegate);
};

}  // namespace

bool ShellBrowserViewDelegate::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowserView> popup_browser_view,
    bool) {
  CefWindow::CreateTopLevelWindow(new PopupWindowDelegate(popup_browser_view));
  return true;
}

// ============================================================================
// Shell
// ============================================================================

Shell& Shell::Get() {
  static Shell* instance = new Shell();
  return *instance;
}

void Shell::Start() {
  CEF_REQUIRE_UI_THREAD();
  ApplyPendingWipes();
  RegisterUiScheme();
  ui_view_ = CreateUiView();
  CefWindow::CreateTopLevelWindow(new ShellWindowDelegate(ui_view_));
}

CefRefPtr<CefBrowserView> Shell::CreateUiView() {
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 8, 9, 11);  // #08090B — без белой вспышки
  return CefBrowserView::CreateBrowserView(new UiClient(), kUiUrl, settings,
                                           nullptr, nullptr,
                                           new ShellBrowserViewDelegate());
}

void Shell::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  window_->SetTitle(kAppName);
  if (auto icon = LoadWindowIcon()) {
    window_->SetWindowIcon(icon);
    window_->SetWindowAppIcon(icon);
  }
  // AddOverlayView() добавляет в корень окна пустой views::View («z-order reference»).
  // При дефолтном FillLayout он растягивается на всё окно и перехватывает hit-test Views,
  // из‑за чего NSView интерфейса на macOS перестаёт получать мышь. BoxLayout с flex только
  // у UI-вида оставляет таким служебным видам нулевую ширину.
  CefBoxLayoutSettings bl;
  bl.horizontal = true;
  bl.inside_border_insets = CefInsets(0, 0, 0, 0);
  bl.between_child_spacing = 0;
  bl.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  bl.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  bl.minimum_cross_axis_size = 0;
  bl.default_flex = 0;
  CefRefPtr<CefBoxLayout> layout = window_->SetToBoxLayout(bl);
  window_->AddChildView(ui_view_);
  if (layout) layout->SetFlexForView(ui_view_, 1);
  window_->Show();
  ui_view_->RequestFocus();
}

bool Shell::CanCloseWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!closing_) {
    closing_ = true;
    std::vector<std::string> ids;
    for (auto& kv : tabs_) ids.push_back(kv.first);
    for (auto& id : ids) DestroyTab(id);
    for (auto& w : std::set<CefRefPtr<CefWindow>>(popup_windows_)) w->Close();
  }
  CefRefPtr<CefBrowser> b = ui_view_ ? ui_view_->GetBrowser() : nullptr;
  return b ? b->GetHost()->TryCloseBrowser() : true;
}

void Shell::OnWindowDestroyed() {
  CEF_REQUIRE_UI_THREAD();
  window_ = nullptr;
  ui_view_ = nullptr;
  closing_ = true;
  for (auto& w : std::set<CefRefPtr<CefWindow>>(popup_windows_)) w->Close();
  MaybeQuit();
}

void Shell::RequestClose() {
  CEF_REQUIRE_UI_THREAD();
  if (window_) {
    window_->Close();
  } else {
    closing_ = true;
    MaybeQuit();
  }
}

void Shell::OnBrowserCreated() { ++browser_count_; }

void Shell::OnBrowserClosed() {
  if (browser_count_ > 0) --browser_count_;
  MaybeQuit();
}

// Выходим из message loop, когда окна нет и все браузеры уничтожены.
void Shell::MaybeQuit() {
  if (closing_ && !window_ && browser_count_ == 0 && !quit_posted_) {
    quit_posted_ = true;
    CefQuitMessageLoop();
  }
}

void Shell::TrackPopupWindow(CefRefPtr<CefWindow> window) {
  popup_windows_.insert(window);
}
void Shell::UntrackPopupWindow(CefRefPtr<CefWindow> window) {
  popup_windows_.erase(window);
}

// ---- UI-браузер ------------------------------------------------------------

void Shell::OnUiCreated(CefRefPtr<CefBrowser> browser) { ui_browser_ = browser; }

void Shell::OnUiClosed(CefRefPtr<CefBrowser>) { ui_browser_ = nullptr; }

bool Shell::IsUiBrowser(CefRefPtr<CefBrowser> browser) const {
  return browser && ui_browser_ &&
         browser->GetIdentifier() == ui_browser_->GetIdentifier();
}

void Shell::SetDraggableRegions(const std::vector<CefDraggableRegion>& regions) {
  drag_regions_ = regions;
  if (window_) window_->SetDraggableRegions(regions);
}

void Shell::UiEvent(const std::string& name, const std::string& payload_json) {
  if (!ui_browser_) return;
  CefRefPtr<CefFrame> frame = ui_browser_->GetMainFrame();
  if (!frame || !frame->IsValid()) return;
  const std::string js = "window.__shelterHost&&window.__shelterHost.ev(" +
                         JsString(name) + "," + payload_json + ");";
  frame->ExecuteJavaScript(js, kUiUrl, 0);
}

// ---- контексты (сессии) ----------------------------------------------------

CefRefPtr<CefRequestContext> Shell::ContextFor(const std::string& partition) {
  auto it = contexts_.find(partition);
  if (it != contexts_.end()) return it->second;

  CefRequestContextSettings rs;
  if (partition.rfind("persist:", 0) == 0) {
    const std::string dir = platform::UserDataDir() + "/Profiles/" +
                            SanitizeForPath(partition.substr(8));
    CefString(&rs.cache_path) = dir;
    rs.persist_session_cookies = true;
  }
  // temp:* и прочее — cache_path пуст: контекст только в памяти (без следов на диске).
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::CreateContext(rs, nullptr);
  contexts_[partition] = ctx;
  return ctx;
}

void Shell::ReleaseContextIfUnused(const std::string& partition) {
  if (partition.rfind("temp:", 0) != 0) return;  // persist-контексты держим
  for (auto& kv : tabs_) {
    if (kv.second.partition == partition) return;
  }
  contexts_.erase(partition);
}

void Shell::QueueWipe(const std::string& partition) {
  if (partition.rfind("persist:", 0) != 0) return;
  std::error_code ec;
  fs::create_directories(platform::UserDataDir(), ec);
  std::ofstream f(platform::UserDataDir() + "/pending-wipe.txt", std::ios::app);
  f << SanitizeForPath(partition.substr(8)) << "\n";
}

// Профили, помеченные «сжечь», удаляются при следующем старте (пока они не открыты).
void Shell::ApplyPendingWipes() {
  const std::string list = platform::UserDataDir() + "/pending-wipe.txt";
  std::ifstream f(list);
  if (!f) return;
  std::string line;
  std::error_code ec;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    fs::remove_all(platform::UserDataDir() + "/Profiles/" + line, ec);
  }
  f.close();
  fs::remove(list, ec);
}

// ---- вкладки ---------------------------------------------------------------

Tab* Shell::FindTab(const std::string& id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : &it->second;
}

Tab* Shell::FindTabByBrowser(int browser_id) {
  for (auto& kv : tabs_) {
    if (kv.second.browser_id == browser_id) return &kv.second;
  }
  return nullptr;
}

Tab* Shell::CreateTab(const std::string& id, const std::string& partition,
                      const std::string& url, double zoom) {
  Tab tab;
  tab.id = id;
  tab.partition = partition;
  tab.requested_url = url;
  tab.client = new TabClient(id);

  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  tab.view = CefBrowserView::CreateBrowserView(
      tab.client, url, settings, nullptr, ContextFor(partition),
      new ShellBrowserViewDelegate());
  const bool can_activate =
      !CefCommandLine::GetGlobalCommandLine()->HasSwitch("tab-no-activate");
  tab.overlay = window_->AddOverlayView(tab.view, CEF_DOCKING_MODE_CUSTOM,
                                        can_activate);
  tab.overlay->SetVisible(false);
  zoom_ = zoom;
  tabs_[id] = std::move(tab);
  Tab* t = &tabs_[id];
  if (CefRefPtr<CefBrowser> b = t->view->GetBrowser()) {
    OnTabCreated(b, id);
  }
  return t;
}

void Shell::OnTabCreated(CefRefPtr<CefBrowser> browser, const std::string& id) {
  Tab* t = FindTab(id);
  if (!t || t->browser) return;
  t->browser = browser;
  t->browser_id = browser->GetIdentifier();
  browser->GetHost()->SetZoomLevel(std::log(zoom_) / std::log(1.2));
  if (!t->pending_url.empty()) {
    browser->GetMainFrame()->LoadURL(t->pending_url);
    t->pending_url.clear();
  }
}

void Shell::DestroyTab(const std::string& id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) return;
  Tab tab = std::move(it->second);
  tabs_.erase(it);
  if (active_tab_ == id) active_tab_.clear();
  if (tab.snap_registration) tab.snap_registration = nullptr;
  if (tab.overlay && tab.overlay->IsValid()) tab.overlay->Destroy();
  // Последняя ссылка на BrowserView уходит вместе с tab — браузер закроется сам
  // (OnBeforeClose придёт позже, TabClient к этому моменту уже «отвязан»).
  tab.overlay = nullptr;
  tab.view = nullptr;
  tab.browser = nullptr;
  ReleaseContextIfUnused(tab.partition);
}

void Shell::HideAllTabs() {
  for (auto& kv : tabs_) {
    if (kv.second.overlay && kv.second.overlay->IsValid()) {
      kv.second.overlay->SetVisible(false);
    }
  }
}

void Shell::LayoutTab(Tab* tab, const CefRect& rect, bool visible) {
  if (!tab || !tab->overlay || !tab->overlay->IsValid()) return;
  if (rect.width > 0 && rect.height > 0) {
    tab->overlay->SetBounds(rect);
    last_rect_ = rect;
  }
  tab->overlay->SetVisible(visible && last_rect_.width > 0);
}

CefRefPtr<CefBrowser> Shell::ActiveBrowser() {
  Tab* t = FindTab(active_tab_);
  return t ? t->browser : nullptr;
}

// ---- события вкладок -------------------------------------------------------

namespace {

std::string NormKey(std::string u) {
  auto strip = [&](const char* p) {
    std::string s(p);
    if (u.rfind(s, 0) == 0) u.erase(0, s.size());
  };
  strip("https://");
  strip("http://");
  strip("www.");
  while (!u.empty() && (u.back() == '/' || u.back() == '#')) u.pop_back();
  std::transform(u.begin(), u.end(), u.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return u;
}

}  // namespace

bool SameUrl(const std::string& a, const std::string& b) {
  return NormKey(a) == NormKey(b);
}

void Shell::OnTabAddress(CefRefPtr<CefBrowser> browser, const std::string& url) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  // Страница ошибки (data:) — не адрес вкладки.
  if (!t->error_url.empty() && url.rfind("data:", 0) == 0) return;
  if (url.rfind("data:text/html", 0) != 0) t->error_url.clear();
  t->current_url = url;
  if (SameUrl(url, t->requested_url)) return;
  t->requested_url = url;
  UiEvent("nav", "{\"id\":" + JsString(t->id) + ",\"url\":" + JsString(url) + "}");
}

void Shell::OnTabTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t || title.rfind("data:", 0) == 0) return;
  UiEvent("title",
          "{\"id\":" + JsString(t->id) + ",\"title\":" + JsString(title) + "}");
}

void Shell::OnTabLoading(CefRefPtr<CefBrowser> browser, bool loading) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  t->loading = loading;
  UiEvent("loading", "{\"id\":" + JsString(t->id) + ",\"loading\":" +
                         (loading ? "true" : "false") + "}");
}

void Shell::OnTabNewWindow(CefRefPtr<CefBrowser> browser, const std::string& url) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  UiEvent("newtab", "{\"url\":" + JsString(url) + ",\"from\":" +
                        JsString(t ? t->id : "") + "}");
}

bool Shell::OnTabKey(CefRefPtr<CefBrowser> browser, const CefKeyEvent& e) {
  std::string code, key;
  if (!MapKeyEvent(e, &code, &key)) return false;

#if defined(OS_MAC)
  const bool mod = (e.modifiers & EVENTFLAG_COMMAND_DOWN) != 0;
#else
  const bool mod = (e.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
#endif
  const bool shift = (e.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  const bool alt = (e.modifiers & EVENTFLAG_ALT_DOWN) != 0;

  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return false;

  // Нативные действия над страницей
  if (code == "F12" || (mod && shift && code == "KeyI")) {
    TabAction(t->id, "devtools", "");
    return true;
  }
  if (code == "F5" || (mod && !shift && !alt && code == "KeyR")) {
    TabAction(t->id, "reload", "");
    return true;
  }
  if (mod && shift && code == "KeyR") {
    TabAction(t->id, "reloadHard", "");
    return true;
  }
  if (alt && !mod && (code == "ArrowLeft" || code == "ArrowRight")) {
    // история ведётся в UI — отдаём ему
  } else if (!mod) {
    return false;
  }

  // Горячие клавиши, которые понимает UI (см. keydown в index.html).
  static const std::set<std::string> kForward = {
      "KeyK", "KeyL", "KeyF", "KeyP", "KeyT", "KeyW", "KeyY", "KeyD", "KeyO",
      "KeyI", "Comma", "Backslash", "Equal", "Minus", "NumpadAdd",
      "NumpadSubtract", "Digit0", "Digit1", "Digit2", "Digit3", "Digit4",
      "Tab", "ArrowLeft", "ArrowRight"};
  if (!kForward.count(code)) return false;

  std::ostringstream os;
  os << "{\"code\":" << JsString(code) << ",\"key\":" << JsString(key)
     << ",\"ctrl\":" << ((e.modifiers & EVENTFLAG_CONTROL_DOWN) ? "true" : "false")
     << ",\"meta\":" << ((e.modifiers & EVENTFLAG_COMMAND_DOWN) ? "true" : "false")
     << ",\"shift\":" << (shift ? "true" : "false")
     << ",\"alt\":" << (alt ? "true" : "false") << "}";
  UiEvent("key", os.str());
  return true;
}

void Shell::OnTabContextMenu(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame>,
                             CefRefPtr<CefContextMenuParams> params) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  last_ctx_tab_ = t->id;

  std::string media = "none";
  switch (params->GetMediaType()) {
    case CM_MEDIATYPE_IMAGE: media = "image"; break;
    case CM_MEDIATYPE_VIDEO: media = "video"; break;
    case CM_MEDIATYPE_AUDIO: media = "audio"; break;
    default: break;
  }
  const int flags = params->GetEditStateFlags();
  std::ostringstream os;
  os << "{\"id\":" << JsString(t->id) << ",\"partition\":"
     << JsString(t->partition) << ",\"x\":" << params->GetXCoord()
     << ",\"y\":" << params->GetYCoord() << ",\"params\":{"
     << "\"linkURL\":" << JsString(params->GetLinkUrl().ToString())
     << ",\"srcURL\":" << JsString(params->GetSourceUrl().ToString())
     << ",\"pageURL\":" << JsString(params->GetPageUrl().ToString())
     << ",\"selectionText\":" << JsString(params->GetSelectionText().ToString())
     << ",\"mediaType\":" << JsString(media)
     << ",\"isEditable\":" << (params->IsEditable() ? "true" : "false")
     << ",\"editFlags\":{\"canCut\":"
     << ((flags & CM_EDITFLAG_CAN_CUT) ? "true" : "false")
     << ",\"canCopy\":" << ((flags & CM_EDITFLAG_CAN_COPY) ? "true" : "false")
     << ",\"canPaste\":" << ((flags & CM_EDITFLAG_CAN_PASTE) ? "true" : "false")
     << "}}}";
  UiEvent("ctx", os.str());
}

// ---- загрузки --------------------------------------------------------------

namespace {

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (CefParseURL(url, parts)) return CefString(&parts.host).ToString();
  return std::string();
}

std::string UniquePath(const std::string& dir, const std::string& name) {
  fs::path base = fs::path(dir) / fs::path(name).filename();
  if (!fs::exists(base)) return base.string();
  const std::string stem = base.stem().string();
  const std::string ext = base.extension().string();
  for (int i = 1; i < 1000; ++i) {
    fs::path p = fs::path(dir) / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!fs::exists(p)) return p.string();
  }
  return base.string();
}

}  // namespace

void Shell::OnTabDownloadBefore(CefRefPtr<CefBrowser>,
                                CefRefPtr<CefDownloadItem> item,
                                const std::string& suggested_name,
                                CefRefPtr<CefBeforeDownloadCallback> callback) {
  const std::string id = "dl" + std::to_string(item->GetId());
  std::string name = suggested_name.empty() ? "download" : suggested_name;
  pending_downloads_[id] = {callback, name};

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"size\":" << item->GetTotalBytes()
     << ",\"url\":" << JsString(item->GetURL().ToString())
     << ",\"host\":" << JsString(HostOf(item->GetURL().ToString())) << "}";
  UiEvent("dlprompt", os.str());
}

void Shell::DownloadDecision(const std::string& id, const std::string& action) {
  auto it = pending_downloads_.find(id);
  if (it == pending_downloads_.end()) return;
  PendingDownload pd = it->second;
  pending_downloads_.erase(it);
  if (action == "save" || action == "saveAs") {
    std::error_code ec;
    fs::create_directories(platform::DownloadsDir(), ec);
    pd.callback->Continue(UniquePath(platform::DownloadsDir(), pd.filename),
                          action == "saveAs");
  }
  // "cancel": callback освобождается без Continue — загрузка отменяется.
}

void Shell::OnTabDownloadUpdated(CefRefPtr<CefDownloadItem> item) {
  const std::string id = "dl" + std::to_string(item->GetId());
  std::string state = "progressing";
  if (item->IsComplete()) {
    state = "completed";
  } else if (item->IsCanceled()) {
    state = "cancelled";
  } else if (!item->IsInProgress()) {
    state = "interrupted";
  }
  std::string name = item->GetSuggestedFileName().ToString();
  const std::string path = item->GetFullPath().ToString();
  if (name.empty() && !path.empty()) name = fs::path(path).filename().string();
  if (name.empty()) name = "download";

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"state\":" << JsString(state)
     << ",\"bytes\":" << item->GetReceivedBytes()
     << ",\"total\":" << item->GetTotalBytes()
     << ",\"path\":" << JsString(path) << "}";
  UiEvent("download", os.str());
}

}  // namespace shelter
