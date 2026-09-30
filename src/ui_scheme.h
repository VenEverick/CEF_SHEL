// SHELTER — обработчик схемы shelter://app/ (отдаёт файлы UI из resources/ui).
#ifndef SHELTER_UI_SCHEME_H_
#define SHELTER_UI_SCHEME_H_

namespace shelter {

// Регистрирует фабрику схемы. Вызывать в browser-процессе после инициализации CEF.
void RegisterUiScheme();

}  // namespace shelter

#endif  // SHELTER_UI_SCHEME_H_
