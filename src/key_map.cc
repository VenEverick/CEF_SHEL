#include "src/key_map.h"

namespace shelter {

bool MapKeyEvent(const CefKeyEvent& e, std::string* code, std::string* key) {
  const int vk = e.windows_key_code;  // на macOS тоже заполняется (VK_*)
  if (vk >= 'A' && vk <= 'Z') {
    *code = std::string("Key") + static_cast<char>(vk);
    *key = std::string(1, static_cast<char>(vk - 'A' + 'a'));
    return true;
  }
  if (vk >= '0' && vk <= '9') {
    *code = std::string("Digit") + static_cast<char>(vk);
    *key = std::string(1, static_cast<char>(vk));
    return true;
  }
  struct Entry {
    int vk;
    const char* code;
    const char* key;
  };
  static const Entry kTable[] = {
      {0x09, "Tab", "Tab"},
      {0x1B, "Escape", "Escape"},
      {0x25, "ArrowLeft", "ArrowLeft"},
      {0x26, "ArrowUp", "ArrowUp"},
      {0x27, "ArrowRight", "ArrowRight"},
      {0x28, "ArrowDown", "ArrowDown"},
      {0x6B, "NumpadAdd", "+"},
      {0x6D, "NumpadSubtract", "-"},
      {0xBB, "Equal", "="},
      {0xBC, "Comma", ","},
      {0xBD, "Minus", "-"},
      {0xBE, "Period", "."},
      {0xBF, "Slash", "/"},
      {0xDC, "Backslash", "\\"},
      {0x70, "F1", "F1"},
      {0x74, "F5", "F5"},
      {0x7B, "F12", "F12"},
  };
  for (const auto& en : kTable) {
    if (en.vk == vk) {
      *code = en.code;
      *key = en.key;
      return true;
    }
  }
  return false;
}

}  // namespace shelter
