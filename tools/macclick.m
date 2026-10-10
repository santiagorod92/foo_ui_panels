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
    if (strcmp(argv[1], "drag") == 0) {
        if (argc < 6) { fprintf(stderr, "usage: macclick drag <x> <y> <x2> <y2>\n"); return 2; }
        CGPoint q = CGPointMake(atof(argv[4]), atof(argv[5]));
        post(kCGEventLeftMouseDown, p, kCGMouseButtonLeft, 1);
        usleep(80000);
        const int steps = 20;
        for (int i = 1; i <= steps; ++i) {
            CGPoint m = CGPointMake(p.x + (q.x - p.x) * i / steps, p.y + (q.y - p.y) * i / steps);
            post(kCGEventLeftMouseDragged, m, kCGMouseButtonLeft, 1);
            usleep(15000);
        }
        usleep(80000);
        post(kCGEventLeftMouseUp, q, kCGMouseButtonLeft, 1);
        return 0;
    }
    const int clicks = strcmp(argv[1], "dblclick") == 0 ? 2 : 1;
    for (int i = 1; i <= clicks; ++i) {
        post(kCGEventLeftMouseDown, p, kCGMouseButtonLeft, i);
        usleep(30000);
        post(kCGEventLeftMouseUp, p, kCGMouseButtonLeft, i);
        usleep(60000);
    }
    return 0;
}
