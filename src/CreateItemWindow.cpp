#include "CreateItemWindow.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include <Shlguid.h>
#include <shlobj.h>

namespace chromabrowse {

const wchar_t IPreviewHandlerIID[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";

bool isTextFile(wchar_t *ext);
bool previewHandlerCLSID(wchar_t *ext, CLSID *previewID);

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item) {
    CComPtr<ItemWindow> window;
    SFGAOF attr;
    if (checkHR(item->GetAttributes(SFGAO_FOLDER, &attr)) && (attr & SFGAO_FOLDER)) {
        window.Attach(new FolderWindow(parent, item));
        return window;
    }
    CComHeapPtr<wchar_t> parsingName;
    if (checkHR(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parsingName))) {
        wchar_t *ext = PathFindExtension(parsingName);
        if (ext) {
            if (isTextFile(ext)) {
                window.Attach(new TextWindow(parent, item));
                return window;
            }
            CLSID previewID;
            if (previewHandlerCLSID(ext, &previewID)) {
                window.Attach(new PreviewWindow(parent, item, previewID));
                return window;
            }
        }
    }
    window.Attach(new ThumbnailWindow(parent, item));
    return window;
}

bool isTextFile(wchar_t *ext) {
    PERCEIVED perceived;
    PERCEIVEDFLAG flags;
    if (checkHR(AssocGetPerceivedType(ext, &perceived, &flags, nullptr)))
        return perceived == PERCEIVED_TYPE_TEXT;
    return false;
}

bool previewHandlerCLSID(wchar_t *ext, CLSID *previewID) {
    // https://geelaw.blog/entries/ipreviewhandlerframe-wpf-1-ui-assoc/
    wchar_t resultGUID[64];
    DWORD resultLen = 64;
    if (FAILED(AssocQueryString(ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_NOTRUNCATE,
            ASSOCSTR_SHELLEXTENSION, ext, IPreviewHandlerIID, resultGUID, &resultLen)))
        return false;
    debugPrintf(L"Found preview handler for %s: %s\n", ext, resultGUID);
    return checkHR(CLSIDFromString(resultGUID, previewID));
}

CComPtr<IShellItem> resolveLink(HWND hwnd, CComPtr<IShellItem> linkItem) {
    // https://stackoverflow.com/a/46064112
    CComPtr<IShellLink> link;
    if (SUCCEEDED(linkItem->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) {
        DWORD resolveFlags = SLR_UPDATE;
        if (hwnd == nullptr)
            resolveFlags |= SLR_NO_UI;
        if (checkHR(link->Resolve(hwnd, resolveFlags))) {
            CComHeapPtr<ITEMIDLIST> targetPIDL;
            if (checkHR(link->GetIDList(&targetPIDL))) {
                CComPtr<IShellItem> targetItem;
                if (checkHR(SHCreateShellItem(nullptr, nullptr, targetPIDL, &targetItem))) {
                    // don't need to recurse, shortcuts to shortcuts are not allowed
                    return targetItem;
                }
            }
        }
    }
    return linkItem;
}

} // namespace
