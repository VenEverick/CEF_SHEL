// SHELTER — преобразование CefKeyEvent в DOM KeyboardEvent.code/key
// (для проброса горячих клавиш из вкладок в UI).
#ifndef SHELTER_KEY_MAP_H_
#define SHELTER_KEY_MAP_H_

#include <string>

#include "include/cef_base.h"
#include "include/internal/cef_types.h"

namespace shelter {

// Возвращает false, если клавиша не нужна UI.
bool MapKeyEvent(const CefKeyEvent& event, std::string* code, std::string* key);

}  // namespace shelter

#endif  // SHELTER_KEY_MAP_H_
