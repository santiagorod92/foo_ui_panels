// macclick — post synthetic mouse events to drive the macOS UI in dev/testing, the way
// tools/wclick.c drives the Wine build. Screen coordinates, points, origin top-left.
//   macclick move <x> <y>          move the cursor (hover)
//   macclick click <x> <y>         left click
//   macclick dblclick <x> <y>      left double click
// Build: clang -framework ApplicationServices -framework Foundation -o /tmp/macclick tools/macclick.m
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>

static void post(CGEventType t, CGPoint p, CGMouseButton b, int clicks) {
    CGEventRef e = CGEventCreateMouseEvent(NULL, t, p, b);
    if (clicks > 1) CGEventSetIntegerValueField(e, kCGMouseEventClickState, clicks);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

int main(int argc, const char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: macclick move|click|dblclick <x> <y>\n"); return 2; }
    CGPoint p = CGPointMake(atof(argv[2]), atof(argv[3]));
    post(kCGEventMouseMoved, p, kCGMouseButtonLeft, 1);
    if (strcmp(argv[1], "move") == 0) return 0;
    const int clicks = strcmp(argv[1], "dblclick") == 0 ? 2 : 1;
    for (int i = 1; i <= clicks; ++i) {
        post(kCGEventLeftMouseDown, p, kCGMouseButtonLeft, i);
        usleep(30000);
        post(kCGEventLeftMouseUp, p, kCGMouseButtonLeft, i);
        usleep(60000);
    }
    return 0;
}
