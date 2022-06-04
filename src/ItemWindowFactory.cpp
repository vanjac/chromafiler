#include "ItemWindowFactory.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include <Shlguid.h>

namespace chromabrowse {

const wchar_t *IPreviewHandlerIID = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";

bool previewHandlerCLSID(CComPtr<IShellItem> item, CLSID *previewID);

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item) {
    CComPtr<ItemWindow> window;
    SFGAOF attr;
    CLSID previewID;
    if (SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attr)) && (attr & SFGAO_FOLDER)) {
        window.Attach(new FolderWindow(parent, item));
    } else if (previewHandlerCLSID(item, &previewID)) {
        window.Attach(new PreviewWindow(parent, item, previewID));
    } else {
        window.Attach(new ThumbnailWindow(parent, item));
    }
    return window;
}

bool previewHandlerCLSID(CComPtr<IShellItem> item, CLSID *previewID) {
    // https://geelaw.blog/entries/ipreviewhandlerframe-wpf-1-ui-assoc/
    CComHeapPtr<wchar_t> path;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return false;
    wchar_t *ext = PathFindExtension(path);
    if (!ext)
        return false;
    wchar_t resultGUID[64];
    DWORD resultLen = 64;
    if (FAILED(AssocQueryString(ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_NOTRUNCATE,
            ASSOCSTR_SHELLEXTENSION, ext, IPreviewHandlerIID, resultGUID, &resultLen)))
        return false;
    debugPrintf(L"Found preview handler for %s: %s\n", ext, resultGUID);
    return SUCCEEDED(CLSIDFromString(resultGUID, previewID));
}

} // namespace
