#!/usr/bin/env bash
# Собирает установочный DMG для macOS: SHELTER.app + ярлык «Applications».
# usage: make_dmg.sh <path/to/Shelter.app> <out.dmg> [version]
#
# Подпись: ad-hoc (без сертификата Apple Developer). Вложенные части подписываются
# «изнутри наружу»; для распространения без предупреждений Gatekeeper нужна подпись
# Developer ID и нотаризация (см. README).
set -euo pipefail

APP="$1"
OUT="$2"
VER="${3:-1.0.160}"

[ -d "$APP" ] || { echo "нет приложения: $APP"; exit 1; }

STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/SHELTER.app"
BUNDLE="$STAGE/SHELTER.app"

# Убираем карантинные/служебные атрибуты, если они есть.
xattr -cr "$BUNDLE" || true

echo "== подпись (ad-hoc), изнутри наружу"
FW="$BUNDLE/Contents/Frameworks"
# 1) динамические библиотеки внутри CEF-фреймворка
find "$FW/Chromium Embedded Framework.framework" -type f \( -name "*.dylib" -o -name "*.so" \) \
  -print0 | while IFS= read -r -d '' f; do codesign --force --sign - "$f"; done
# 2) сам фреймворк
codesign --force --sign - "$FW/Chromium Embedded Framework.framework"
# 3) вспомогательные приложения
for h in "$FW"/*.app; do codesign --force --sign - "$h"; done
# 4) главное приложение
codesign --force --sign - "$BUNDLE"
codesign --verify --deep --strict --verbose=2 "$BUNDLE"

ln -s /Applications "$STAGE/Applications"

echo "== создание DMG"
rm -f "$OUT"
hdiutil create -volname "SHELTER $VER" -srcfolder "$STAGE" -ov -format UDZO -fs HFS+ "$OUT"
hdiutil verify "$OUT"
ls -la "$OUT"
rm -rf "$STAGE"
