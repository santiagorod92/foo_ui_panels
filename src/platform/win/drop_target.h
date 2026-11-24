#pragma once
#include "win_sdk.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <functional>

namespace pui::win {

class MainDropTarget : public IDropTarget {
public:
    using Forward = std::function<int(int kind, POINT client, DWORD keys, DWORD offered, IDataObject* data)>;
    MainDropTarget(HWND wnd, Forward forward) : m_wnd(wnd), m_forward(std::move(forward)) {}
    virtual ~MainDropTarget() { hold(nullptr); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&m_refs);
        if (!n) delete this;
        return (ULONG)n;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        FORMATETC f{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        m_files = data && data->QueryGetData(&f) == S_OK;
        hold(data);
        return route(0, keys, pt, effect, data);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override { return route(1, keys, pt, effect, m_data); }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        m_forward(2, POINT{}, 0, DROPEFFECT_NONE, nullptr);
        hold(nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        const HRESULT hr = route(3, keys, pt, effect, data);
        hold(nullptr);
        return hr;
    }

private:
    void hold(IDataObject* data) {
        if (data) data->AddRef();
        if (m_data) m_data->Release();
        m_data = data;
    }
    HRESULT route(int kind, DWORD keys, POINTL pt, DWORD* effect, IDataObject* data) {
        POINT p{ pt.x, pt.y };
        ScreenToClient(m_wnd, &p);
        const int handled = m_forward(kind, p, keys, *effect, data);
        if (handled >= 0) {
            *effect = (DWORD)handled;
            return S_OK;
        }
        *effect = m_files ? *effect & DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (kind == 3 && *effect != DROPEFFECT_NONE && data) deliver(data, POINT{ pt.x, pt.y });
        return S_OK;
    }
    void deliver(IDataObject* data, POINT screen) {
        FORMATETC f{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM m{};
        if (FAILED(data->GetData(&f, &m))) return;
        HWND target = WindowFromPoint(screen);
        while (target && target != m_wnd && !(GetWindowLongW(target, GWL_EXSTYLE) & WS_EX_ACCEPTFILES)) target = GetParent(target);
        if (!target) target = m_wnd;
        const SIZE_T size = GlobalSize(m.hGlobal);
        HGLOBAL copy = size >= sizeof(DROPFILES) ? GlobalAlloc(GHND, size) : nullptr;
        void* src = copy ? GlobalLock(m.hGlobal) : nullptr;
        void* dst = src ? GlobalLock(copy) : nullptr;
        if (dst) {
            std::memcpy(dst, src, size);
            POINT client = screen;
            ScreenToClient(target, &client);
            static_cast<DROPFILES*>(dst)->pt = client;
            static_cast<DROPFILES*>(dst)->fNC = FALSE;
            GlobalUnlock(copy);
        }
        if (src) GlobalUnlock(m.hGlobal);
        ReleaseStgMedium(&m);
        if (dst) SendMessageW(target, WM_DROPFILES, (WPARAM)copy, 0);
        else if (copy) GlobalFree(copy);
    }

    HWND m_wnd;
    Forward m_forward;
    LONG m_refs = 1;
    bool m_files = false;
    IDataObject* m_data = nullptr;
};

}
