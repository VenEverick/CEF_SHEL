#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

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


static int g_forced_key = 0;
static int g_redirected = 0;
static bool g_redirecting = false;
static NSWindow* g_overlay_win = nil;  // окно вида вкладки (не retain)
static std::vector<std::array<int, 4>> g_holes;
static int g_hole_view_w = 0, g_hole_view_h = 0;

static bool InHole(NSEvent* e) {
  NSWindow* w = e.window;
  if (!w || w != g_overlay_win || g_holes.empty() || !w.contentView) return false;
  const CGFloat W = w.contentView.frame.size.width, H = w.contentView.frame.size.height;
  const double sx = g_hole_view_w > 0 ? W / g_hole_view_w : 1, sy = g_hole_view_h > 0 ? H / g_hole_view_h : 1;
  const double x = e.locationInWindow.x / sx, y = (H - e.locationInWindow.y) / sy;
  for (const auto& h : g_holes) {
    if (x >= h[0] && x < h[0] + h[2] && y >= h[1] && y < h[1] + h[3]) return true;
  }
  return false;
}

// Прозрачный участок («дыра») окна вкладки пропускает мышь окну интерфейса под ним.
static NSEvent* RedirectToUi(NSEvent* e) {
  NSWindow* w = e.window;
  const NSPoint sp = [w convertPointToScreen:e.locationInWindow];
  NSWindow* ui = nil;
  for (NSWindow* c in [NSApp windows]) {
    if (c == w || !c.isVisible || !c.canBecomeKeyWindow) continue;
    NSString* cn = NSStringFromClass([c class]);
    if (![cn containsString:@"NativeWidgetMac"] && ![cn hasPrefix:@"Cef"]) continue;
    if (NSPointInRect(sp, c.frame)) { ui = c; break; }
  }
  if (!ui) return e;
  if ((e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown) && !ui.isKeyWindow)
    [ui makeKeyWindow];
  NSEvent* ne = [NSEvent mouseEventWithType:e.type
                                   location:[ui convertPointFromScreen:sp]
                              modifierFlags:e.modifierFlags
                                  timestamp:e.timestamp
                               windowNumber:ui.windowNumber
                                    context:nil
                                eventNumber:e.eventNumber
                                 clickCount:e.clickCount
                                   pressure:e.pressure];
  if (!ne) return e;
  g_redirected++;
  [ui sendEvent:ne];
  return nil;
}
static id g_mouse_monitor = nil;

void SetClipLevel(int) {}
std::string DumpWindowChain(void*) { return std::string(); }

void InstallInputFixes() {
  if (g_mouse_monitor) return;
  g_mouse_monitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                                            NSEventMaskOtherMouseDown | NSEventMaskLeftMouseUp |
                                            NSEventMaskRightMouseUp | NSEventMaskLeftMouseDragged |
                                            NSEventMaskMouseMoved)
                                   handler:^NSEvent*(NSEvent* e) {
                                     const NSEventType t = e.type;
                                     if (t == NSEventTypeLeftMouseDown || t == NSEventTypeRightMouseDown) {
                                       if (InHole(e)) { g_redirecting = true; return RedirectToUi(e); }
                                     } else if (t == NSEventTypeLeftMouseUp || t == NSEventTypeRightMouseUp ||
                                                t == NSEventTypeLeftMouseDragged) {
                                       if (g_redirecting && e.window == g_overlay_win) {
                                         if (t != NSEventTypeLeftMouseDragged) g_redirecting = false;
                                         return RedirectToUi(e);
                                       }
                                       return e;
                                     } else if (t == NSEventTypeMouseMoved) {
                                       if (InHole(e)) return RedirectToUi(e);
                                       return e;
                                     }
                                     NSWindow* w = e.window;
                                     if (w && w.isVisible && !w.isKeyWindow && w.canBecomeKeyWindow &&
                                         ![NSApp modalWindow]) {
                                       NSString* cn = NSStringFromClass([w class]);
                                       if ([cn containsString:@"NativeWidgetMac"] || [cn hasPrefix:@"Cef"]) {
                                         // Окно приложения не в фокусе (например, фокус у окна вкладки):
                                         // делаем его ключевым ДО доставки события, иначе клик тратится
                                         // на активацию и приходится кликать дважды.
                                         [w makeKeyWindow];
                                         g_forced_key++;
                                       }
                                     }
                                     return e;
                                   }];
  [g_mouse_monitor retain];
}

void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 4>>& holes, int view_w, int view_h) {
  NSView* v = (NSView*)handle;
  NSWindow* win = v ? v.window : nil;
  if (!win || !win.contentView) return;
  g_overlay_win = win;
  g_holes = holes;
  g_hole_view_w = view_w;
  g_hole_view_h = view_h;
  NSView* root = win.contentView.superview ? win.contentView.superview : win.contentView;
  bool any = !holes.empty();
  for (int i = 0; i < 4; ++i) any = any || radii[i] > 0.5;
  if (!any) {
    if (root.layer) root.layer.mask = nil;
    return;
  }
  root.wantsLayer = YES;
  CALayer* layer = root.layer;
  if (!layer) return;
  const CGFloat W = win.contentView.frame.size.width, H = win.contentView.frame.size.height;
  const CGFloat sx = view_w > 0 ? W / view_w : 1, sy = view_h > 0 ? H / view_h : 1;
  const CGFloat tl = radii[0] * sx, tr = radii[1] * sx, br = radii[2] * sx, bl = radii[3] * sx;

  // Путь в координатах «сверху вниз», затем переворачиваем под систему координат слоя окна.
  CGMutablePathRef p = CGPathCreateMutable();
  CGPathMoveToPoint(p, nullptr, tl, 0);
  CGPathAddLineToPoint(p, nullptr, W - tr, 0);
  if (tr > 0) CGPathAddArc(p, nullptr, W - tr, tr, tr, -M_PI_2, 0, false);
  CGPathAddLineToPoint(p, nullptr, W, H - br);
  if (br > 0) CGPathAddArc(p, nullptr, W - br, H - br, br, 0, M_PI_2, false);
  CGPathAddLineToPoint(p, nullptr, bl, H);
  if (bl > 0) CGPathAddArc(p, nullptr, bl, H - bl, bl, M_PI_2, M_PI, false);
  CGPathAddLineToPoint(p, nullptr, 0, tl);
  if (tl > 0) CGPathAddArc(p, nullptr, tl, tl, tl, M_PI, 3 * M_PI_2, false);
  CGPathCloseSubpath(p);
  for (const auto& h : holes) {
    CGPathAddRect(p, nullptr, CGRectMake(h[0] * sx, h[1] * sy, h[2] * sx, h[3] * sy));
  }
  CGAffineTransform flip = CGAffineTransformMake(1, 0, 0, -1, 0, H);
  CGPathRef flipped = CGPathCreateCopyByTransformingPath(p, &flip);
  CGPathRelease(p);

  CAShapeLayer* mask = [CAShapeLayer layer];
  mask.frame = layer.bounds;
  mask.path = flipped;
  CGColorRef black = CGColorCreateGenericGray(0, 1);
  mask.fillColor = black;
  CGColorRelease(black);
  mask.fillRule = kCAFillRuleEvenOdd;
  CGPathRelease(flipped);
  layer.mask = mask;
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
    [out appendFormat:@"forcedKey=%d redirected=%d\n", g_forced_key, g_redirected];
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
