#include "CreateItemWindow.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "Settings.h"
#include "ShellUtils.h"
#include "UIStrings.h"
#include <Shlguid.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <VersionHelpers.h>
#include <propkey.h>

namespace chromafiler {

const wchar_t IPreviewHandlerIID[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
// Windows TXT Previewer {1531d583-8375-4d3f-b5fb-d23bbd169f22}
const CLSID TXT_PREVIEWER_CLSID =
    {0x1531d583, 0x8375, 0x4d3f, {0xb5, 0xfb, 0xd2, 0x3b, 0xbd, 0x16, 0x9f, 0x22}};

bool previewHandlerCLSID(wchar_t *ext, CLSID *previewID);

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item) {
    CComPtr<ItemWindow> window;
    SFGAOF attr;
    if (checkHR(item->GetAttributes(SFGAO_FOLDER, &attr)) && (attr & SFGAO_FOLDER)) {
        window.Attach(new FolderWindow(parent, item));
        return window;
    }

    bool previewsEnabled = settings::getPreviewsEnabled();
    bool textEditorEnabled = settings::getTextEditorEnabled();
    CComHeapPtr<wchar_t> parentRelAddr;
    // SIGDN_PARENTRELATIVEFORADDRESSBAR will always have the extension
    // TODO: use IShellItem2::GetString instead?
    if ((previewsEnabled || textEditorEnabled)
            && checkHR(item->GetDisplayName(SIGDN_PARENTRELATIVEFORADDRESSBAR, &parentRelAddr))) {
        if (wchar_t *ext = PathFindExtension(parentRelAddr)) {
            CLSID previewID;
            if (ext[0] == 0) {
                if (textEditorEnabled) {
                    CComQIPtr<IShellItem2> item2(item);
                    CComHeapPtr<wchar_t> str;
                    if (item2 && SUCCEEDED(item2->GetString(PKEY_ItemType, &str))
                            && lstrcmp(str, L".") == 0) {
                        window.Attach(new TextWindow(parent, item));
                        return window;
                    }
                }
            } else if (previewHandlerCLSID(ext, &previewID)) {
                if (textEditorEnabled && previewID == TXT_PREVIEWER_CLSID) {
                    window.Attach(new TextWindow(parent, item));
                    return window;
                } else if (previewsEnabled) {
                    window.Attach(new PreviewWindow(parent, item, previewID));
                    return window;
                }
            }
        }
    }
    window.Attach(new ThumbnailWindow(parent, item));
    return window;
}

bool previewHandlerCLSID(wchar_t *ext, CLSID *previewID) {
    // https://geelaw.blog/entries/ipreviewhandlerframe-wpf-1-ui-assoc/
    wchar_t resultGUID[64];
    DWORD resultLen = _countof(resultGUID);
    if (FAILED(AssocQueryString(ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_NOTRUNCATE,
            ASSOCSTR_SHELLEXTENSION, ext, IPreviewHandlerIID, resultGUID, &resultLen)))
        return false;
    debugPrintf(L"Found preview handler for %s: %s\n", ext, resultGUID);
    return checkHR(CLSIDFromString(resultGUID, previewID));
}

CComPtr<IShellItem> resolveLink(CComPtr<IShellItem> linkItem) {
    // https://stackoverflow.com/a/46064112
    CComPtr<IShellLink> link;
    if (SUCCEEDED(linkItem->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) {
        if (checkHR(link->Resolve(nullptr, SLR_NO_UI | SLR_UPDATE))) {
            CComHeapPtr<ITEMIDLIST> targetPIDL;
            if (checkHR(link->GetIDList(&targetPIDL))) {
                CComPtr<IShellItem> targetItem;
                if (checkHR(SHCreateItemFromIDList(targetPIDL, IID_PPV_ARGS(&targetItem)))) {
                    SFGAOF attr;
                    if (FAILED(targetItem->GetAttributes(SFGAO_VALIDATE, &attr))) // doesn't exist
                        return linkItem;
                    // don't need to recurse, shortcuts to shortcuts are not allowed
                    return targetItem;
                }
            }
        }
    }
    return linkItem;
}

CComPtr<IShellItem> itemFromPath(wchar_t *path) {
    while (1) {
        CComPtr<IShellItem> item;
        // parse name vs display name https://stackoverflow.com/q/42966489
        if (checkHR(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&item))))
            return item;
        int result = MessageBox(nullptr, formatString(IDS_CANT_FIND_ITEM, path).get(),
            getString(IDS_ERROR_CAPTION), MB_CANCELTRYCONTINUE | MB_ICONERROR);
        if (result == IDCANCEL) {
            return nullptr;
        } else if (result == IDCONTINUE) {
            if (checkHR(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                    IID_PPV_ARGS(&item))))
                return item;
        } // else retry
    }
}

CComPtr<IShellItem> createScratchFile(CComPtr<IShellItem> folder) {
    CComPtr<IFileOperation> operation;
    if (!checkHR(operation.CoCreateInstance(__uuidof(FileOperation))))
        return nullptr;
    checkHR(operation->SetOperationFlags(
        (IsWindows8OrGreater() ? FOFX_ADDUNDORECORD : FOF_ALLOWUNDO) | FOF_RENAMEONCOLLISION));
    wstr_ptr fileName = settings::getScratchFileName();
    NewItemSink eventSink;
    checkHR(operation->NewItem(folder, FILE_ATTRIBUTE_NORMAL, fileName.get(), nullptr, &eventSink));
    checkHR(operation->PerformOperations());
    return eventSink.newItem;
}

bool isCFWindow(HWND hwnd) {
    wchar_t className[64] = L"";
    if (!checkLE(GetClassName(hwnd, className, _countof(className))))
        return false;
    const wchar_t prefix[] = L"ChromaFile";
    className[_countof(prefix) - 1] = 0;
    return lstrcmpi(className, prefix) == 0;
}


void debugDisplayNames(HWND hwnd, CComPtr<IShellItem> item) {
    static SIGDN nameTypes[] = {
        SIGDN_NORMALDISPLAY, SIGDN_PARENTRELATIVE,
        SIGDN_PARENTRELATIVEEDITING, SIGDN_PARENTRELATIVEFORUI,
        SIGDN_PARENTRELATIVEFORADDRESSBAR, SIGDN_FILESYSPATH,
        SIGDN_DESKTOPABSOLUTEEDITING, SIGDN_DESKTOPABSOLUTEPARSING,
        SIGDN_PARENTRELATIVEPARSING, SIGDN_URL
    };
    CComHeapPtr<wchar_t> names[_countof(nameTypes)];
    for (int i = 0; i < _countof(names); i++)
        checkHR(item->GetDisplayName(nameTypes[i], &names[i]));
    static const PROPERTYKEY *pkeys[] = {
        // https://learn.microsoft.com/en-us/windows/win32/properties/core-bumper
        &PKEY_ItemName, &PKEY_ItemNameDisplay,
        &PKEY_ItemNameDisplayWithoutExtension,
        &PKEY_ItemPathDisplay, &PKEY_ItemType,
        &PKEY_FileName, &PKEY_FileExtension,
        // https://learn.microsoft.com/en-us/windows/win32/properties/shell-bumper
        &PKEY_NamespaceCLSID,
    };
    CComHeapPtr<wchar_t> props[_countof(pkeys)];
    CComQIPtr<IShellItem2> item2(item);
    if (item2) {
        for (int i = 0; i < _countof(pkeys); i++)
            checkHR(item2->GetString(*pkeys[i], &props[i]));
    }
    showDebugMessage(hwnd, L"Item Display Names", L""
        "Normal Display:\t\t%1\r\n"             "Parent Relative:\t\t%2\r\n"
        "Parent Relative Editing:\t%3\r\n"      "Parent Relative UI (Win8):\t%4\r\n"
        "Parent Relative Address Bar:\t%5\r\n"  "File System Path:\t\t%6\r\n"
        "Desktop Absolute Editing:\t%7\r\n"     "Desktop Absolute Parsing:\t%8\r\n"
        "Parent Relative Parsing:\t%9\r\n"      "URL:\t\t\t%10\r\n"
        "System.ItemName:\t\t%11\r\n"           "System.ItemNameDisplay:\t%12\r\n"
        "System.ItemNameDisplayWithoutExtension: %13\r\n"
        "System.ItemPathDisplay:\t%14\r\n"      "System.ItemType:\t\t%15\r\n"
        "System.FileName:\t\t%16\r\n"           "System.FileExtension:\t%17\r\n"
        "System.NamespaceCLSID:\t%18",
        &*names[0], &*names[1], &*names[2], &*names[3], &*names[4], &*names[5],
        &*names[6], &*names[7], &*names[8], &*names[9],
        &*props[0], &*props[1], &*props[2], &*props[3], &*props[4], &*props[5],
        &*props[6], &*props[7]);
}

} // namespace
