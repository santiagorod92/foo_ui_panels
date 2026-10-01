// WM_DROPFILES helper shared by the panel hosts (win_view.cpp) and the main window (main.cpp):
// the dropped paths as UTF-8 and the drop point in the receiving window's client coordinates.
// The caller still owns the HDROP (DragFinish).
#pragma once
#include "win_sdk.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace pui::win {

inline std::vector<std::string> dropped_files(HDROP drop, POINT& pt) {
    std::vector<std::string> out;
    DragQueryPoint(drop, &pt);
    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring w(len, L'\0');
        if (!len || DragQueryFileW(drop, i, &w[0], len + 1) != len) continue;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], bytes, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace pui::win
