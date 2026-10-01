#include "src/ui_scheme.h"

#include <fstream>
#include <sstream>
#include <string>

#include "include/cef_parser.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "src/common.h"
#include "src/platform.h"

namespace shelter {

namespace {

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

void ReplaceAll(std::string* s, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from.size(), to);
    pos += to.size();
  }
}

// Безопасный относительный путь внутри каталога UI.
bool SanitizeRelPath(std::string* rel) {
  if (rel->empty() || *rel == "/") *rel = "index.html";
  if ((*rel)[0] == '/') rel->erase(0, 1);
  if (rel->find("..") != std::string::npos) return false;
  for (char c : *rel) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
              c == '/';
    if (!ok) return false;
  }
  return true;
}

// Мост подключается в index.html без правки самого файла: тег <script>
// вставляется перед самым первым <script>, то есть до кода UI (но после CSP <meta>).
std::string InjectBridge(std::string html) {
  const std::string tag = "<script src=\"host-bridge.js\"></script>\n";
  size_t pos = html.find("<script");
  if (pos == std::string::npos) pos = html.find("</head>");
  if (pos == std::string::npos) return tag + html;
  html.insert(pos, tag);
  return html;
}

// Небольшой «читающий» хук состояния (space/ghost по id вкладки). Вставляется при отдаче
// страницы — файл дизайна на диске остаётся нетронутым. Если маркер не найден (дизайн
// обновили), host-bridge.js работает в упрощённом режиме.
void InjectStateHook(std::string* html) {
  const std::string marker = "window.getActiveTabId = () => S.activeTabId;";
  size_t pos = html->find(marker);
  if (pos == std::string::npos) return;
  const std::string hook =
      "\nwindow.__shelterTab = id => { const ks = Object.keys(S.spaces || {});"
      " for (const k of ks) { const t = (S.spaces[k].tabs || []).find(x => x.id === id);"
      " if (t) return { space: k, ghost: !!(t.ghost || S.ghost) }; }"
      " return { space: S.currentSpace, ghost: !!S.ghost }; };";
  html->insert(pos + marker.size(), hook);
}

class UiSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();

    // UI-страница доступна только из нашего UI-браузера и никогда — из вкладок.
    const std::string url = request->GetURL().ToString();
    std::string rel = url.substr(std::string(kUiOriginPrefix).size() - 1);
    size_t cut = rel.find_first_of("?#");
    if (cut != std::string::npos) rel.resize(cut);
    if (!SanitizeRelPath(&rel)) return NotFound();

    std::string data;
    if (!ReadFile(platform::UiResourceDir() + "/" + rel, &data)) {
      return NotFound();
    }

    if (rel == "index.html") {
      InjectStateHook(&data);
      data = InjectBridge(std::move(data));
    } else if (rel == "host-bridge.js") {
      ReplaceAll(&data, "__PLATFORM__", kPlatform);
      ReplaceAll(&data, "__APP_VERSION__", kAppVersion);
      ReplaceAll(&data, "__SECRET_KEY__", platform::SecretKeyHex());
    }

    std::string ext;
    size_t dot = rel.find_last_of('.');
    if (dot != std::string::npos) ext = rel.substr(dot + 1);
    std::string mime = CefGetMimeType(ext);
    if (mime.empty()) mime = "application/octet-stream";

    CefRefPtr<CefStreamReader> stream =
        CefStreamReader::CreateForData(data.data(), data.size());
    CefResponse::HeaderMap headers;
    headers.insert({"Cache-Control", "no-store"});
    return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
  }

 private:
  static CefRefPtr<CefResourceHandler> NotFound() {
    // Пустое тело: отсутствующий необязательный скрипт (native.js и т.п.) не должен
    // исполняться как «Not found».
    static const char kEmpty[] = " ";
    CefRefPtr<CefStreamReader> stream =
        CefStreamReader::CreateForData(const_cast<char*>(kEmpty), 1);
    return new CefStreamResourceHandler(404, "Not Found", "text/plain",
                                        CefResponse::HeaderMap(), stream);
  }

  IMPLEMENT_REFCOUNTING(UiSchemeHandlerFactory);
};

}  // namespace

void RegisterUiScheme() {
  CefRegisterSchemeHandlerFactory(kUiScheme, kUiHost,
                                  new UiSchemeHandlerFactory());
}

}  // namespace shelter
