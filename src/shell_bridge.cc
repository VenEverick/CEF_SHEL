// SHELTER — мост UI -> native (window.shelterNative / window.shelter* хуки из host-bridge.js).
#include <algorithm>
#include <cmath>
#include <sstream>

#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include "src/clients.h"
#include "src/common.h"
#include "src/platform.h"
#include "src/shell.h"

namespace shelter {

namespace {

using Callback = CefMessageRouterBrowserSide::Callback;

double Num(CefRefPtr<CefDictionaryValue> d, const char* key, double def = 0) {
  if (!d || !d->HasKey(key)) return def;
  switch (d->GetType(key)) {
    case VTYPE_INT: return d->GetInt(key);
    case VTYPE_DOUBLE: return d->GetDouble(key);
    case VTYPE_BOOL: return d->GetBool(key) ? 1 : 0;
    default: return def;
  }
}

bool Flag(CefRefPtr<CefDictionaryValue> d, const char* key, bool def) {
  if (!d || !d->HasKey(key)) return def;
  return d->GetType(key) == VTYPE_BOOL ? d->GetBool(key) : Num(d, key) != 0;
}

std::string Str(CefRefPtr<CefDictionaryValue> d, const char* key) {
  if (!d || !d->HasKey(key) || d->GetType(key) != VTYPE_STRING) return "";
  return d->GetString(key).ToString();
}

CefRect RectOf(CefRefPtr<CefDictionaryValue> args) {
  if (!args->HasKey("rect") || args->GetType("rect") != VTYPE_DICTIONARY) {
    return CefRect();
  }
  CefRefPtr<CefDictionaryValue> r = args->GetDictionary("rect");
  return CefRect(static_cast<int>(std::lround(Num(r, "x"))),
                 static_cast<int>(std::lround(Num(r, "y"))),
                 static_cast<int>(std::lround(Num(r, "w"))),
                 static_cast<int>(std::lround(Num(r, "h"))));
}

// Нормализация адреса для навигации вкладки. Схема shelter:// и javascript:
// во вкладках запрещены (иначе страница получила бы доступ к UI).
std::string NavUrl(std::string url) {
  auto b = url.find_first_not_of(" \t\r\n");
  auto e = url.find_last_not_of(" \t\r\n");
  url = b == std::string::npos ? "" : url.substr(b, e - b + 1);
  if (url.empty()) return "about:blank";

  std::string lower = url;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto has = [&](const char* p) { return lower.rfind(p, 0) == 0; };
  if (has("javascript:") || has(std::string(std::string(kUiScheme) + ":").c_str())) {
    return "about:blank";
  }
  bool has_scheme = false;
  for (size_t i = 0; i < lower.size(); ++i) {
    char c = lower[i];
    if (c == ':') { has_scheme = i > 0; break; }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
          c == '-' || c == '.')) break;
  }
  return has_scheme ? url : "https://" + url;
}

// ---- снимок вкладки через DevTools-протокол --------------------------------

class SnapObserver : public CefDevToolsMessageObserver {
 public:
  void Add(int message_id, CefRefPtr<Callback> cb) { pending_[message_id] = cb; }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id,
                              bool success, const void* result,
                              size_t result_size) override {
    auto it = pending_.find(message_id);
    if (it == pending_.end()) return;
    CefRefPtr<Callback> cb = it->second;
    pending_.erase(it);
    if (!success) {
      cb->Failure(500, "capture failed");
      return;
    }
    CefRefPtr<CefValue> v = CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (v && v->GetType() == VTYPE_DICTIONARY &&
        v->GetDictionary()->HasKey("data")) {
      cb->Success("data:image/jpeg;base64," +
                  v->GetDictionary()->GetString("data").ToString());
    } else {
      cb->Failure(500, "no data");
    }
  }

 private:
  std::map<int, CefRefPtr<Callback>> pending_;
  IMPLEMENT_REFCOUNTING(SnapObserver);
};

// ---- очистка cookie с фильтром по времени ----------------------------------

class CookieClearVisitor : public CefCookieVisitor {
 public:
  explicit CookieClearVisitor(double begin_ms) : begin_ms_(begin_ms) {}
  bool Visit(const CefCookie& cookie, int, int, bool& deleteCookie) override {
    if (begin_ms_ <= 0) {
      deleteCookie = true;
    } else {
      const double created_ms =
          static_cast<double>(cookie.creation.val) / 1000.0 - 11644473600000.0;
      deleteCookie = created_ms >= begin_ms_;
    }
    return true;
  }

 private:
  double begin_ms_;
  IMPLEMENT_REFCOUNTING(CookieClearVisitor);
};

}  // namespace

// ============================================================================

bool Shell::HandleBridge(CefRefPtr<CefBrowser>, const std::string& m,
                         CefRefPtr<CefDictionaryValue> a,
                         CefRefPtr<Callback> cb) {
  CEF_REQUIRE_UI_THREAD();

  // ---- окно ----
  if (m == "win.minimize") {
    if (window_) window_->Minimize();
    cb->Success("{}");
    return true;
  }
  if (m == "win.maximize") {
    if (window_) {
      if (window_->IsMaximized()) window_->Restore(); else window_->Maximize();
    }
    cb->Success("{}");
    return true;
  }
  if (m == "win.close") {
    RequestClose();
    cb->Success("{}");
    return true;
  }
  if (m == "win.openPlain") {  // «Открыть в новом окне» — пока вкладкой
    UiEvent("newtab", "{\"url\":" + JsString(Str(a, "url")) + "}");
    cb->Success("{}");
    return true;
  }

  // ---- вкладки ----
  if (m == "view.open") {
    const std::string id = Str(a, "id");
    const std::string url = Str(a, "url");
    const std::string partition = Str(a, "partition");
    if (id.empty() || !window_) {
      cb->Failure(400, "bad args");
      return true;
    }
    const double zoom = Num(a, "zoom", 1.0);
    Tab* t = FindTab(id);
    if (t && t->partition != partition) {  // сменился профиль (пространство/Призрак)
      DestroyTab(id);
      t = nullptr;
    }
    bool created = false, navigated = false;
    const std::string nav = NavUrl(url);
    if (!t) {
      t = CreateTab(id, partition, nav, zoom);
      created = navigated = true;
    } else if (!SameUrl(url, t->requested_url) && !SameUrl(url, t->current_url)) {
      t->requested_url = url;
      t->error_url.clear();
      if (t->browser) {
        t->browser->GetMainFrame()->LoadURL(nav);
      } else {
        t->pending_url = nav;
      }
      navigated = true;
    }
    active_tab_ = id;
    for (auto& kv : tabs_) {
      if (kv.first != id && kv.second.overlay && kv.second.overlay->IsValid()) {
        kv.second.overlay->SetVisible(false);
      }
    }
    LayoutTab(t, RectOf(a), Flag(a, "visible", true));
    if (Flag(a, "focus", true) && t->view) t->view->RequestFocus();
    std::ostringstream os;
    os << "{\"created\":" << (created ? "true" : "false") << ",\"navigated\":"
       << (navigated ? "true" : "false") << ",\"loading\":"
       << (t->loading ? "true" : "false") << "}";
    cb->Success(os.str());
    return true;
  }
  if (m == "view.layout") {
    Tab* t = FindTab(active_tab_);
    if (t) LayoutTab(t, RectOf(a), Flag(a, "visible", true));
    cb->Success("{}");
    return true;
  }
  if (m == "view.clip") {  // скругление углов и «дыры» (оверлеи UI поверх страницы)
    for (int i = 0; i < 4; ++i) clip_radii_[i] = 0;
    clip_holes_.clear();
    if (a->HasKey("radii") && a->GetType("radii") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("radii");
      for (size_t i = 0; i < 4 && i < l->GetSize(); ++i) {
        const int ty = l->GetType(i);
        clip_radii_[i] = ty == VTYPE_DOUBLE ? l->GetDouble(i) : ty == VTYPE_INT ? l->GetInt(i) : 0;
      }
    }
    if (a->HasKey("holes") && a->GetType("holes") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("holes");
      for (size_t i = 0; i < l->GetSize() && i < 16; ++i) {
        if (l->GetType(i) != VTYPE_LIST) continue;
        CefRefPtr<CefListValue> r = l->GetList(i);
        if (r->GetSize() < 4) continue;
        std::array<int, 4> h{};
        for (size_t k = 0; k < 4; ++k) {
          const int ty = r->GetType(k);
          h[k] = static_cast<int>(std::lround(ty == VTYPE_DOUBLE ? r->GetDouble(k) : ty == VTYPE_INT ? r->GetInt(k) : 0));
        }
        clip_holes_.push_back(h);
      }
    }
    ApplyClip(FindTab(active_tab_));
    cb->Success("{}");
    return true;
  }
  if (m == "view.hide") {
    HideAllTabs();
    if (ui_view_) ui_view_->RequestFocus();  // клавиатура — обратно в UI
    cb->Success("{}");
    return true;
  }
  if (m == "view.show") {  // вернуть активную вкладку после снимка/меню
    Tab* t = FindTab(active_tab_);
    if (t) {
      LayoutTab(t, RectOf(a), true);
      if (Flag(a, "focus", true) && t->view) t->view->RequestFocus();
    }
    cb->Success("{}");
    return true;
  }
  if (m == "view.sync") {
    std::set<std::string> keep;
    if (a->HasKey("ids") && a->GetType("ids") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("ids");
      for (size_t i = 0; i < l->GetSize(); ++i) {
        if (l->GetType(i) == VTYPE_STRING) keep.insert(l->GetString(i).ToString());
      }
    }
    std::vector<std::string> drop;
    for (auto& kv : tabs_) {
      if (!keep.count(kv.first)) drop.push_back(kv.first);
    }
    for (auto& id : drop) DestroyTab(id);
    cb->Success("{}");
    return true;
  }
  if (m == "view.act" || m == "ctx.act") {
    std::string id = Str(a, "id");
    if (id.empty()) id = active_tab_;
    TabAction(id, Str(a, "act"), Str(a, "url"));
    cb->Success("{}");
    return true;
  }
  if (m == "view.snap") {
    CaptureSnapshot(Str(a, "id").empty() ? active_tab_ : Str(a, "id"),
                    static_cast<int>(Num(a, "q", 72)), cb);
    return true;
  }
  if (m == "view.zoom") {
    SetZoomAll(Num(a, "factor", 1.0));
    cb->Success("{}");
    return true;
  }
  if (m == "find") {  // поиск по странице вкладки (нативный)
    Tab* t = FindTab(Str(a, "id"));
    if (t && t->browser) {
      if (Str(a, "act") == "stop") {
        t->browser->GetHost()->StopFinding(true);
      } else {
        const std::string text = Str(a, "q");
        if (text.empty()) {
          t->browser->GetHost()->StopFinding(true);
        } else {
          const bool next = Flag(a, "next", false);
          if (!next) {
            t->find_text = text;
            t->find_kick = true;
          }
          t->browser->GetHost()->Find(text, Flag(a, "forward", true), false,
                                      next);
        }
      }
    }
    cb->Success("{}");
    return true;
  }
  if (m == "devtools") {
    if (Str(a, "act") == "window") TabAction(active_tab_, "devtools", "");
    cb->Success("{\"ok\":false}");
    return true;
  }

  // ---- система ----
  if (m == "clip.read") {
    cb->Success("{\"text\":" + JsString(platform::ClipboardRead()) + "}");
    return true;
  }
  if (m == "clip.write") {
    const bool ok = platform::ClipboardWrite(Str(a, "text"));
    cb->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (m == "theme.scheme") {
    platform::SetColorScheme(Str(a, "mode") != "light");
    cb->Success("{}");
    return true;
  }
  if (m == "shell.show") {
    platform::ShowInFolder(Str(a, "path"));
    cb->Success("{}");
    return true;
  }
  if (m == "dl.decision") {
    DownloadDecision(Str(a, "id"), Str(a, "action"));
    cb->Success("{}");
    return true;
  }
  if (m == "privacy.clear") {
    ClearPrivacy(a->HasKey("parts") && a->GetType("parts") == VTYPE_LIST
                     ? a->GetList("parts")
                     : nullptr,
                 a->HasKey("opts") && a->GetType("opts") == VTYPE_DICTIONARY
                     ? a->GetDictionary("opts")
                     : nullptr,
                 cb);
    return true;
  }
  if (m == "dbg.hit") {  // диагностика hitTest (macOS), только для CI
    cb->Success(JsString(platform::DebugHitTest(Num(a, "x", 0), Num(a, "y", 0))));
    return true;
  }
  if (m == "dbg.clip") {  // диагностика (CI): на каком предке HWND держать область
    Tab* t = FindTab(active_tab_);
    if (t && t->browser) {
      // сбросить область на прежнем окне
      double zr[4] = {0, 0, 0, 0};
      std::vector<std::array<int, 4>> none;
      platform::ApplyViewClip(reinterpret_cast<void*>(t->browser->GetHost()->GetWindowHandle()), zr, none,
                              std::max(1, last_rect_.width), std::max(1, last_rect_.height));
      if (a->HasKey("level")) platform::SetClipLevel(static_cast<int>(Num(a, "level", -1)));
      ApplyClip(t);
      cb->Success(JsString(platform::DumpWindowChain(
          reinterpret_cast<void*>(t->browser->GetHost()->GetWindowHandle()))));
    } else {
      cb->Success("\"no tab\"");
    }
    return true;
  }
  if (m == "auth.window") {  // TODO(этап 3): окно входа SHELTER ID
    cb->Success("{\"ok\":false}");
    return true;
  }
  return false;
}

// ---- действия над вкладкой -------------------------------------------------

void Shell::TabAction(const std::string& id, const std::string& act,
                      const std::string& url) {
  Tab* t = FindTab(id);
  if (!t || !t->browser) return;
  CefRefPtr<CefBrowser> b = t->browser;
  CefRefPtr<CefFrame> f = b->GetFocusedFrame();
  if (!f) f = b->GetMainFrame();

  if (act == "reload" || act == "reloadHard") {
    if (!t->error_url.empty()) {
      b->GetMainFrame()->LoadURL(t->error_url);
    } else if (act == "reloadHard") {
      b->ReloadIgnoreCache();
    } else {
      b->Reload();
    }
  } else if (act == "stop") {
    b->StopLoad();
  } else if (act == "print") {
    b->GetHost()->Print();
  } else if (act == "devtools") {
    CefWindowInfo wi;
    CefBrowserSettings bs;
    b->GetHost()->ShowDevTools(wi, nullptr, bs, CefPoint());
  } else if (act == "cut") {
    f->Cut();
  } else if (act == "copy") {
    f->Copy();
  } else if (act == "paste") {
    f->Paste();
  } else if (act == "selectAll") {
    f->SelectAll();
  } else if (act == "download" && !url.empty()) {
    b->GetHost()->StartDownload(url);
  }
}

void Shell::SetZoomAll(double factor) {
  factor = std::min(5.0, std::max(0.25, factor));
  zoom_ = factor;
  const double level = std::log(factor) / std::log(1.2);
  for (auto& kv : tabs_) {
    if (kv.second.browser) kv.second.browser->GetHost()->SetZoomLevel(level);
  }
}

void Shell::CaptureSnapshot(const std::string& tab_id, int quality,
                            CefRefPtr<Callback> cb) {
  Tab* t = FindTab(tab_id);
  if (!t || !t->browser) {
    cb->Failure(404, "no tab");
    return;
  }
  if (!t->snap_observer) {
    t->snap_observer = new SnapObserver();
    t->snap_registration =
        t->browser->GetHost()->AddDevToolsMessageObserver(t->snap_observer);
  }
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "jpeg");
  params->SetInt("quality", std::min(100, std::max(10, quality)));
  params->SetBool("optimizeForSpeed", true);
  const int id = t->browser->GetHost()->ExecuteDevToolsMethod(
      0, "Page.captureScreenshot", params);
  if (id == 0) {
    cb->Failure(500, "devtools unavailable");
    return;
  }
  static_cast<SnapObserver*>(t->snap_observer.get())->Add(id, cb);
}

// ---- «Удалить данные» ------------------------------------------------------

void Shell::ClearPrivacy(CefRefPtr<CefListValue> parts,
                         CefRefPtr<CefDictionaryValue> opts,
                         CefRefPtr<Callback> cb) {
  std::set<std::string> mask;
  bool full = true;  // без mask (кнопка «Сжечь») — полная очистка раздела
  double begin = 0;
  if (opts && opts->HasKey("mask") && opts->GetType("mask") == VTYPE_LIST) {
    full = false;
    CefRefPtr<CefListValue> l = opts->GetList("mask");
    for (size_t i = 0; i < l->GetSize(); ++i) {
      if (l->GetType(i) == VTYPE_STRING) mask.insert(l->GetString(i).ToString());
    }
    begin = Num(opts, "begin", 0);
  }

  std::vector<std::string> names;
  if (parts) {
    for (size_t i = 0; i < parts->GetSize(); ++i) {
      if (parts->GetType(i) == VTYPE_STRING) names.push_back(parts->GetString(i).ToString());
    }
  } else {
    for (auto& kv : contexts_) names.push_back(kv.first);
  }

  auto wants = [&](const char* k) { return full || mask.count(k) > 0; };

  for (const auto& p : names) {
    auto it = contexts_.find(p);
    if (it != contexts_.end()) {
      if (wants("COOKIES")) {
        CefRefPtr<CefCookieManager> cm = it->second->GetCookieManager(nullptr);
        if (cm) cm->VisitAllCookies(new CookieClearVisitor(full ? 0 : begin));
      }
      for (auto& kv : tabs_) {
        Tab& t = kv.second;
        if (t.partition != p || !t.browser) continue;
        if (wants("CACHE")) {
          t.browser->GetHost()->ExecuteDevToolsMethod(0, "Network.clearBrowserCache", nullptr);
        }
        if (wants("LOCAL_STORAGE") || wants("INDEXED_DB") || wants("SERVICE_WORKERS")) {
          CefURLParts up;
          if (CefParseURL(t.current_url, up)) {
            const std::string origin = CefString(&up.origin).ToString();
            if (!origin.empty()) {
              std::string types;
              auto add = [&](const char* s) { types += (types.empty() ? "" : ",") + std::string(s); };
              if (wants("LOCAL_STORAGE")) add("local_storage");
              if (wants("INDEXED_DB")) add("indexeddb");
              if (wants("SERVICE_WORKERS")) { add("service_workers"); add("cache_storage"); }
              CefRefPtr<CefDictionaryValue> dp = CefDictionaryValue::Create();
              dp->SetString("origin", origin);
              dp->SetString("storageTypes", types);
              t.browser->GetHost()->ExecuteDevToolsMethod(0, "Storage.clearDataForOrigin", dp);
            }
          }
        }
      }
    }
    if (full) QueueWipe(p);  // каталог профиля удалится при следующем запуске
  }
  cb->Success("{\"ok\":true}");
}

}  // namespace shelter
