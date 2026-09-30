// SHELTER — вспомогательные процессы macOS (renderer / GPU / plugin / alerts).
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"
#include "src/renderer_app.h"

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return 1;
  }
  CefMainArgs main_args(argc, argv);
  CefRefPtr<CefApp> app(new shelter::RendererApp());
  return CefExecuteProcess(main_args, app, nullptr);
}
