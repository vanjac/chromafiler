#include "CreateItemWindow.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "Settings.h"
#include "resource.h"
#include <Shlguid.h>
#include <shlobj.h>
#include <strsafe.h>

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
            if (settings::getTextEditorEnabled() && isTextFile(ext)) {
                window.Attach(new TextWindow(parent, item));
                return window;
            }
            CLSID previewID;
            if (settings::getPreviewsEnabled() && previewHandlerCLSID(ext, &previewID)) {
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
                if (checkHR(SHCreateItemFromIDList(targetPIDL, IID_PPV_ARGS(&targetItem)))) {
                    // don't need to recurse, shortcuts to shortcuts are not allowed
                    return targetItem;
                }
            }
        }
    }
    return linkItem;
}

CComPtr<IShellItem> itemFromPath(wchar_t *path) {
    CComPtr<IShellItem> item;
    while (1) {
        // parse name vs display name https://stackoverflow.com/q/42966489
        if (checkHR(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&item))))
            break;
        wchar_t caption[64], message[MAX_PATH + 64];
        LoadString(GetModuleHandle(nullptr), IDS_ERROR_CAPTION, caption, _countof(caption));
        LoadString(GetModuleHandle(nullptr), IDS_CANT_FIND_ITEM, message, _countof(message));
        StringCchCat(message, _countof(message), path);
        int result = MessageBox(nullptr, message, caption, MB_CANCELTRYCONTINUE | MB_ICONERROR);
        if (result == IDCANCEL) {
            return nullptr;
        } else if (result == IDCONTINUE) {
            if (checkHR(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                    IID_PPV_ARGS(&item))))
                break;
        } // else retry
    }
    return item;
}

} // namespace
