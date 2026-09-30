// SHELTER — общие константы и мелкие утилиты.
#ifndef SHELTER_COMMON_H_
#define SHELTER_COMMON_H_

#include <string>

#include "include/cef_values.h"

namespace shelter {

inline constexpr char kAppName[] = "SHELTER";
inline constexpr char kAppVersion[] = "1.0.160";

// Внутренняя схема, по которой отдаётся chrome-UI (resources/ui/*).
// Зарегистрирована как standard + secure + cors + fetch, поэтому у UI есть
// нормальный origin (localStorage, navigator.clipboard, CSP 'self').
inline constexpr char kUiScheme[] = "shelter";
inline constexpr char kUiHost[] = "app";
inline constexpr char kUiUrl[] = "shelter://app/index.html";
inline constexpr char kUiOriginPrefix[] = "shelter://app/";

// Платформа в терминах window.shelterNative.platform (как process.platform).
#if defined(OS_MAC)
inline constexpr char kPlatform[] = "darwin";
#elif defined(OS_WIN)
inline constexpr char kPlatform[] = "win32";
#else
inline constexpr char kPlatform[] = "linux";
#endif

// JSON <-> CefValue
std::string ToJson(CefRefPtr<CefValue> value);
std::string ToJson(CefRefPtr<CefDictionaryValue> dict);
CefRefPtr<CefValue> ParseJson(const std::string& json);

// Строка в виде JS-литерала (с кавычками), безопасная для вставки в скрипт.
std::string JsString(const std::string& s);

// Имя раздела (partition) -> безопасное имя каталога.
std::string SanitizeForPath(const std::string& s);

}  // namespace shelter

#endif  // SHELTER_COMMON_H_
