#include "CreateItemWindow.h"
#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TextWindow.h"
#include "Settings.h"
#include "UIStrings.h"
#include <Shlguid.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>

namespace chromafile {

const wchar_t IPreviewHandlerIID[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
const wchar_t CONTROL_PANEL_PATH[] = L"::{26EE0668-A00A-44D7-9371-BEB064C98683}";
// Windows TXT Previewer {1531d583-8375-4d3f-b5fb-d23bbd169f22}
const CLSID TXT_PREVIEWER_CLSID =
    {0x1531d583, 0x8375, 0x4d3f, {0xb5, 0xfb, 0xd2, 0x3b, 0xbd, 0x16, 0x9f, 0x22}};

bool previewHandlerCLSID(wchar_t *ext, CLSID *previewID);

CComPtr<ItemWindow> createItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item) {
    CComHeapPtr<wchar_t> parsingName;
    checkHR(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parsingName));

    CComPtr<ItemWindow> window;
    SFGAOF attr;
    if (checkHR(item->GetAttributes(SFGAO_FOLDER, &attr)) && (attr & SFGAO_FOLDER)) {
        if (lstrcmpi(parsingName, CONTROL_PANEL_PATH) == 0) {
            window.Attach(new ThumbnailWindow(parent, item));
            return window;
        }
        window.Attach(new FolderWindow(parent, item));
        return window;
    } else if (parsingName) {
        bool previewsEnabled = settings::getPreviewsEnabled();
        bool textEditorEnabled = settings::getTextEditorEnabled();
        if (previewsEnabled || textEditorEnabled) {
            wchar_t *ext = PathFindExtension(parsingName);
            if (ext) {
                if (textEditorEnabled && ext[0] == 0) {
                    window.Attach(new TextWindow(parent, item));
                    return window;
                }
                CLSID previewID;
                if (previewHandlerCLSID(ext, &previewID)) {
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
    while (1) {
        CComPtr<IShellItem> item;
        // parse name vs display name https://stackoverflow.com/q/42966489
        if (checkHR(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&item))))
            return item;
        LocalHeapPtr<wchar_t> caption, message;
        formatMessage(caption, STR_ERROR_CAPTION);
        formatMessage(message, STR_CANT_FIND_ITEM, path);
        int result = MessageBox(nullptr, message, caption, MB_CANCELTRYCONTINUE | MB_ICONERROR);
        if (result == IDCANCEL) {
            return nullptr;
        } else if (result == IDCONTINUE) {
            if (checkHR(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                    IID_PPV_ARGS(&item))))
                return item;
        } // else retry
    }
}


// not reference counted!
class NewItemSink : public IFileOperationProgressSink {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID, void **) override;
    STDMETHODIMP_(ULONG) AddRef() { return 2; };
    STDMETHODIMP_(ULONG) Release() { return 1; };

    // IFileOperationProgressSink
    STDMETHODIMP PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *)
        override;

    STDMETHODIMP StartOperations() override {return S_OK;}
    STDMETHODIMP FinishOperations(HRESULT) override {return S_OK;}
    STDMETHODIMP PreRenameItem(DWORD, IShellItem *, LPCWSTR) override {return S_OK;}
    STDMETHODIMP PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override {return S_OK;}
    STDMETHODIMP PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override {return S_OK;}
    STDMETHODIMP PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override {return S_OK;}
    STDMETHODIMP PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override {return S_OK;}
    STDMETHODIMP PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *)
        override {return S_OK;}
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem *) override {return S_OK;}
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem *, HRESULT, IShellItem *) override {return S_OK;}
    STDMETHODIMP PreNewItem(DWORD, IShellItem *, LPCWSTR) override {return S_OK;}
    STDMETHODIMP UpdateProgress(UINT, UINT) override {return S_OK;}
    STDMETHODIMP ResetTimer() override {return S_OK;}
    STDMETHODIMP PauseTimer() override {return S_OK;}
    STDMETHODIMP ResumeTimer() override {return S_OK;}

    CComPtr<IShellItem> newItem;
};

CComPtr<IShellItem> createScratchFile(CComPtr<IShellItem> folder) {
    CComPtr<IFileOperation> operation;
    if (!checkHR(operation.CoCreateInstance(__uuidof(FileOperation))))
        return nullptr;
    NewItemSink eventSink;
    DWORD eventSinkCookie = 0;
    checkHR(operation->Advise(&eventSink, &eventSinkCookie));
    // TODO: FOFX_ADDUNDORECORD requires Windows 8
    checkHR(operation->SetOperationFlags(FOFX_ADDUNDORECORD | FOF_RENAMEONCOLLISION));
    CComHeapPtr<wchar_t> fileName;
    settings::getScratchFileName(fileName);
    checkHR(operation->NewItem(folder, FILE_ATTRIBUTE_NORMAL, fileName, nullptr, nullptr));
    checkHR(operation->PerformOperations());
    checkHR(operation->Unadvise(eventSinkCookie));
    return eventSink.newItem;
}

STDMETHODIMP NewItemSink::PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT,
        IShellItem *item) {
    newItem = item;
    return S_OK;
}

STDMETHODIMP NewItemSink::QueryInterface(REFIID id, void **obj) {
    *obj = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(IFileOperationProgressSink)) {
        *obj = this;
        return S_OK;
    }
    return E_NOINTERFACE;
};

} // namespace
