// Kinetik — file drops on the plugin window: Windows implementation (macOS: FileDropMac.mm; Linux: no-op)
#include "FileDrop.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>

namespace kn {
namespace {
struct DropTarget final : public IDropTarget {
    LONG refs = 1;
    HWND hwnd = nullptr;
    FileDropFn fn;
    bool accept = false;

    static bool hasFiles(IDataObject* d)
    {
        FORMATETC f = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return d != nullptr && d->QueryGetData(&f) == S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const LONG r = InterlockedDecrement(&refs); if (r == 0) delete this; return (ULONG)r; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL, DWORD* eff) override
    {
        accept = hasFiles(d); *eff = accept ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override { *eff = accept ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL pt, DWORD* eff) override
    {
        *eff = DROPEFFECT_NONE;
        FORMATETC f = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM m = {};
        if (d == nullptr || d->GetData(&f, &m) != S_OK) return S_OK;
        HDROP h = (HDROP)m.hGlobal;
        const UINT n = DragQueryFileW(h, 0, nullptr, 0);
        if (n > 0) {
            std::wstring w(n + 1, L'\0');
            DragQueryFileW(h, 0, &w[0], n + 1);
            w.resize(n);
            const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            std::string u((size_t)len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], len, nullptr, nullptr);
            POINT p = {pt.x, pt.y};
            ScreenToClient(hwnd, &p);
            RECT rc = {}; GetClientRect(hwnd, &rc);
            const double fx = rc.right > 0 ? (double)p.x / rc.right : 0.5, fy = rc.bottom > 0 ? (double)p.y / rc.bottom : 0.5;
            if (fn) fn(u, fx, fy);
            *eff = DROPEFFECT_COPY;
        }
        ReleaseStgMedium(&m);
        return S_OK;
    }
};
struct Token { HWND hwnd; DropTarget* target; bool ole; };
} // namespace

void* fileDropAttach(uintptr_t view, FileDropFn fn)
{
    HWND hwnd = (HWND)view;
    if (hwnd == nullptr) return nullptr;
    const bool ole = SUCCEEDED(OleInitialize(nullptr));
    DropTarget* t = new DropTarget();
    t->hwnd = hwnd; t->fn = std::move(fn);
    if (RegisterDragDrop(hwnd, t) != S_OK) { t->Release(); if (ole) OleUninitialize(); return nullptr; }
    return new Token{hwnd, t, ole};
}

void fileDropDetach(void* token)
{
    Token* k = (Token*)token;
    if (k == nullptr) return;
    RevokeDragDrop(k->hwnd);
    k->target->Release();
    if (k->ole) OleUninitialize();
    delete k;
}
} // namespace kn

#elif !defined(__APPLE__)
namespace kn {
void* fileDropAttach(uintptr_t, FileDropFn) { return nullptr; }
void fileDropDetach(void*) {}
}
#endif
