#include "src/common.h"

#include <cstdio>

#include "include/cef_parser.h"

namespace shelter {

std::string ToJson(CefRefPtr<CefValue> value) {
  if (!value) {
    return "null";
  }
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string ToJson(CefRefPtr<CefDictionaryValue> dict) {
  CefRefPtr<CefValue> v = CefValue::Create();
  v->SetDictionary(dict);
  return ToJson(v);
}

CefRefPtr<CefValue> ParseJson(const std::string& json) {
  return CefParseJSON(json, JSON_PARSER_RFC);
}

std::string JsString(const std::string& s) {
  // Через CefValue -> JSON: получаем корректно экранированную строку.
  CefRefPtr<CefValue> v = CefValue::Create();
  v->SetString(s);
  std::string out = ToJson(v);
  // U+2028/2029 допустимы в современном JS, но заменим на всякий случай.
  std::string res;
  res.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (i + 2 < out.size() && static_cast<unsigned char>(out[i]) == 0xE2 &&
        static_cast<unsigned char>(out[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(out[i + 2]) == 0xA8 ||
         static_cast<unsigned char>(out[i + 2]) == 0xA9)) {
      res += (static_cast<unsigned char>(out[i + 2]) == 0xA8) ? "\\u2028"
                                                              : "\\u2029";
      i += 2;
    } else {
      res += out[i];
    }
  }
  return res;
}

std::string SanitizeForPath(const std::string& s) {
  std::string r;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      r += c;
    } else {
      r += '_';
    }
  }
  if (r.empty()) {
    r = "default";
  }
  if (r.size() > 80) {
    r.resize(80);
  }
  return r;
}

}  // namespace shelter
