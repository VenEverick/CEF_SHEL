#import <Cocoa/Cocoa.h>

#include "src/platform.h"

namespace shelter {
namespace platform {

std::string UiResourceDir() {
  NSString* res = [[NSBundle mainBundle] resourcePath];
  return std::string([res UTF8String]) + "/ui";
}

std::string UserDataDir() {
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  NSString* base = dirs.count ? dirs[0] : NSHomeDirectory();
  return std::string([base UTF8String]) + "/SHELTER";
}

std::string DownloadsDir() {
  NSArray* dirs = NSSearchPathForDirectoriesInDomains(
      NSDownloadsDirectory, NSUserDomainMask, YES);
  NSString* d = dirs.count ? dirs[0] : NSHomeDirectory();
  return std::string([d UTF8String]);
}

void ShowInFolder(const std::string& path) {
  NSString* p = [NSString stringWithUTF8String:path.c_str()];
  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[
    [NSURL fileURLWithPath:p]
  ]];
}

void OpenExternal(const std::string& url) {
  NSString* u = [NSString stringWithUTF8String:url.c_str()];
  NSURL* nsurl = [NSURL URLWithString:u];
  if (nsurl) [[NSWorkspace sharedWorkspace] openURL:nsurl];
}

std::string ClipboardRead() {
  NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  return s ? std::string([s UTF8String]) : std::string();
}

bool ClipboardWrite(const std::string& text) {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  return [pb setString:[NSString stringWithUTF8String:text.c_str()]
               forType:NSPasteboardTypeString];
}

void SetColorScheme(bool dark) {
  NSAppearance* a = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  [NSApp setAppearance:a];
}


static void DumpView(NSView* v, int depth, NSMutableString* out) {
  if (!v || depth > 5) return;
  NSRect f = v.frame;
  [out appendFormat:@"%*s%@ (%.0f,%.0f %.0fx%.0f)%@\n", depth * 2, "", NSStringFromClass([v class]),
                    f.origin.x, f.origin.y, f.size.width, f.size.height, v.hidden ? @" HIDDEN" : @""];
  for (NSView* c in v.subviews) DumpView(c, depth + 1, out);
}

std::string DebugHitTest(double x, double y) {
  NSMutableString* out = [NSMutableString string];
  for (NSWindow* w in [NSApp windows]) {
    NSView* cv = w.contentView;
    if (!cv || !w.visible) continue;
    NSRect wf = w.frame;
    NSPoint p = NSMakePoint(x, wf.size.height - y);
    NSView* hit = [cv hitTest:p];
    [out appendFormat:@"WINDOW %@ frame(%.0f,%.0f %.0fx%.0f) key=%d main=%d level=%ld parent=%@ children=%lu hit=%@ hitFrame=%@\n",
                      NSStringFromClass([w class]), wf.origin.x, wf.origin.y, wf.size.width, wf.size.height,
                      (int)w.isKeyWindow, (int)w.isMainWindow, (long)w.level, w.parentWindow ? @"yes" : @"no",
                      (unsigned long)w.childWindows.count, hit ? NSStringFromClass([hit class]) : @"nil",
                      hit ? NSStringFromRect(hit.frame) : @""];
    DumpView(cv, 1, out);
  }
  return std::string([out UTF8String]);
}

}  // namespace platform
}  // namespace shelter
