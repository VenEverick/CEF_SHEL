// SHELTER — точка входа macOS (главный процесс).
#import <Cocoa/Cocoa.h>

#include "include/cef_application_mac.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"
#include "src/browser_app.h"
#include "src/shell.h"

@interface ShelterAppDelegate : NSObject <NSApplicationDelegate>
- (void)createApplication:(id)object;
- (void)tryToTerminateApplication:(NSApplication*)app;
@end

@interface ShelterApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation ShelterApplication
- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}
- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}
- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}
- (void)terminate:(id)sender {
  ShelterAppDelegate* delegate = static_cast<ShelterAppDelegate*>([NSApp delegate]);
  [delegate tryToTerminateApplication:self];
  // Не выходим сразу: завершение произойдёт после закрытия всех браузеров.
}
@end

@implementation ShelterAppDelegate

static NSMenuItem* AddItem(NSMenu* menu, NSString* title, SEL action, NSString* key,
                           NSEventModifierFlags mods = NSEventModifierFlagCommand) {
  NSMenuItem* item = [menu addItemWithTitle:title action:action keyEquivalent:key];
  [item setKeyEquivalentModifierMask:mods];
  return item;
}

- (void)buildMenu {
  NSMenu* bar = [[NSMenu alloc] init];

  NSMenuItem* appItem = [[NSMenuItem alloc] init];
  [bar addItem:appItem];
  NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"SHELTER"];
  AddItem(appMenu, @"О программе SHELTER", @selector(orderFrontStandardAboutPanel:), @"");
  [appMenu addItem:[NSMenuItem separatorItem]];
  AddItem(appMenu, @"Скрыть SHELTER", @selector(hide:), @"h");
  AddItem(appMenu, @"Скрыть остальные", @selector(hideOtherApplications:), @"h",
          NSEventModifierFlagCommand | NSEventModifierFlagOption);
  AddItem(appMenu, @"Показать все", @selector(unhideAllApplications:), @"");
  [appMenu addItem:[NSMenuItem separatorItem]];
  AddItem(appMenu, @"Завершить SHELTER", @selector(terminate:), @"q");
  [appItem setSubmenu:appMenu];

  NSMenuItem* editItem = [[NSMenuItem alloc] init];
  [bar addItem:editItem];
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Правка"];
  AddItem(edit, @"Отменить", @selector(undo:), @"z");
  AddItem(edit, @"Повторить", @selector(redo:), @"z",
          NSEventModifierFlagCommand | NSEventModifierFlagShift);
  [edit addItem:[NSMenuItem separatorItem]];
  AddItem(edit, @"Вырезать", @selector(cut:), @"x");
  AddItem(edit, @"Копировать", @selector(copy:), @"c");
  AddItem(edit, @"Вставить", @selector(paste:), @"v");
  AddItem(edit, @"Выбрать все", @selector(selectAll:), @"a");
  [editItem setSubmenu:edit];

  NSMenuItem* winItem = [[NSMenuItem alloc] init];
  [bar addItem:winItem];
  NSMenu* win = [[NSMenu alloc] initWithTitle:@"Окно"];
  AddItem(win, @"Свернуть", @selector(performMiniaturize:), @"m");
  AddItem(win, @"Масштаб", @selector(performZoom:), @"");
  [winItem setSubmenu:win];
  [NSApp setWindowsMenu:win];

  [NSApp setMainMenu:bar];
}

- (void)createApplication:(id)object {
  [self buildMenu];
  [[NSApplication sharedApplication] setDelegate:self];
}

- (void)tryToTerminateApplication:(NSApplication*)app {
  shelter::Shell::Get().RequestClose();
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  return NSTerminateNow;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)theApplication
                    hasVisibleWindows:(BOOL)flag {
  return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
  return YES;
}
@end

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    return 1;
  }
  CefMainArgs main_args(argc, argv);

  @autoreleasepool {
    [ShelterApplication sharedApplication];
    CHECK([NSApp isKindOfClass:[ShelterApplication class]]);

    CefSettings settings;
    shelter::FillSettings(settings);

    CefRefPtr<shelter::BrowserApp> app(new shelter::BrowserApp);

    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
      return CefGetExitCode();
    }

    ShelterAppDelegate* delegate = [[ShelterAppDelegate alloc] init];
    NSApp.delegate = delegate;
    [delegate performSelectorOnMainThread:@selector(createApplication:)
                               withObject:nil
                            waitUntilDone:NO];

    CefRunMessageLoop();
    CefShutdown();
    delegate = nil;
  }
  return 0;
}
