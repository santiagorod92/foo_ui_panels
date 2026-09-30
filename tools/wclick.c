// wclick <x> <y> [top:<window title>] [l|r|dbl|move|close|wheelN|key:VK|type:TEXT]
// Dev tool: posts a mouse/key event to the deepest visible child of the Panels UI main window
// (or another top-level window by title) at client point (x,y). Runs inside Wine next to
// foobar2000, so UI tests never touch the host's real mouse. Built by scripts/ui-test.sh.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: wclick x y [top:title] [l|r|dbl|move|close|wheelN|key:VK]\n"); return 2; }
    int ai = 3;
    HWND top = FindWindowW(L"foo_ui_panels_main", NULL);
    if (argc > ai && strncmp(argv[ai], "top:", 4) == 0) {
        wchar_t t[256]; MultiByteToWideChar(CP_UTF8, 0, argv[ai] + 4, -1, t, 256);
        top = FindWindowW(NULL, t);
        ++ai;
    }
    if (!top) { printf("no window\n"); return 1; }
    const char* what = argc > ai ? argv[ai] : "l";
    if (!strcmp(what, "close")) { PostMessageW(top, WM_CLOSE, 0, 0); return 0; }
    POINT cp = { atoi(argv[1]), atoi(argv[2]) };
    HWND target = top;
    for (;;) {
        HWND ch = ChildWindowFromPointEx(target, cp, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!ch || ch == target) break;
        MapWindowPoints(target, ch, &cp, 1);
        target = ch;
    }
    LPARAM lp = MAKELPARAM(cp.x, cp.y);
    wchar_t cls[128]; GetClassNameW(target, cls, 128);
    printf("target %ls at %ld,%ld\n", cls, cp.x, cp.y);
    PostMessageW(target, WM_MOUSEMOVE, 0, lp);
    if (!strcmp(what, "l")) {
        PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, lp); PostMessageW(target, WM_LBUTTONUP, 0, lp);
    } else if (!strcmp(what, "dbl")) {
        PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, lp); PostMessageW(target, WM_LBUTTONUP, 0, lp);
        PostMessageW(target, WM_LBUTTONDBLCLK, MK_LBUTTON, lp); PostMessageW(target, WM_LBUTTONUP, 0, lp);
    } else if (!strcmp(what, "r")) {
        PostMessageW(target, WM_RBUTTONDOWN, MK_RBUTTON, lp); PostMessageW(target, WM_RBUTTONUP, 0, lp);
    } else if (!strncmp(what, "wheel", 5)) {
        POINT s = cp; ClientToScreen(target, &s);
        PostMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(0, atoi(what + 5) * WHEEL_DELTA), MAKELPARAM(s.x, s.y));
    } else if (!strncmp(what, "type:", 5)) {
        for (const char* c = what + 5; *c; ++c) PostMessageW(target, WM_CHAR, (WPARAM)(unsigned char)*c, 0);
    } else if (!strncmp(what, "key:", 4)) {
        PostMessageW(target, WM_KEYDOWN, (WPARAM)strtol(what + 4, NULL, 0), 0);
    }
    return 0;
}
