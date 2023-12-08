#include "ProxyIcon.h"
#include "ItemWindow.h"
#include "GeomUtils.h"
#include "WinUtils.h"
#include "DPI.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <vssym32.h>
#include <strsafe.h>
#include <propkey.h>
#include <VersionHelpers.h>

namespace chromafiler {

// dimensions
static SIZE PROXY_PADDING = {7, 3};
static SIZE PROXY_INFLATE = {4, 1};

// this produces the color used in every high-contrast theme
// regular light mode theme uses #999999
const BYTE INACTIVE_CAPTION_ALPHA = 156;

const BYTE PROXY_BUTTON_STYLE = BTNS_DROPDOWN | BTNS_NOPREFIX;

static HFONT captionFont = nullptr;

static CComPtr<IDropTargetHelper> dropTargetHelper;

void ProxyIcon::init() {
    PROXY_PADDING = scaleDPI(PROXY_PADDING);
    PROXY_INFLATE = scaleDPI(PROXY_INFLATE);
}

void ProxyIcon::initTheme(HTHEME theme) {
    LOGFONT logFont;
    if (checkHR(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont)))
        captionFont = CreateFontIndirect(&logFont);
}

void ProxyIcon::initMetrics(const NONCLIENTMETRICS &metrics) {
    captionFont = CreateFontIndirect(&metrics.lfCaptionFont);
}

void ProxyIcon::uninit() {
    if (captionFont)
        DeleteFont(captionFont);
}

ProxyIcon::ProxyIcon(ItemWindow *outer) : outer(outer) {}

void ProxyIcon::create(HWND parent, IShellItem *item, wchar_t *title, int top, int height) {
    bool layered = IsWindows8OrGreater();
    toolbar = CreateWindowEx(layered ? WS_EX_LAYERED : 0, TOOLBARCLASSNAME, nullptr,
        TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_REGISTERDROP
            | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (layered)
        SetLayeredWindowAttributes(toolbar, 0, INACTIVE_CAPTION_ALPHA, LWA_ALPHA);

    SendMessage(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    if (captionFont)
        SendMessage(toolbar, WM_SETFONT, (WPARAM)captionFont, FALSE);
    SendMessage(toolbar, TB_SETPADDING, 0, MAKELPARAM(PROXY_PADDING.cx, PROXY_PADDING.cy));

    TBBUTTON proxyButton = {0, IDM_PROXY_BUTTON, TBSTATE_ENABLED,
        PROXY_BUTTON_STYLE | BTNS_AUTOSIZE, {}, 0, (INT_PTR)(wchar_t *)title};
    SendMessage(toolbar, TB_ADDBUTTONS, 1, (LPARAM)&proxyButton);

    SIZE ideal;
    SendMessage(toolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
    if (IsWindows10OrGreater()) {
        // center vertically
        int buttonHeight = GET_Y_LPARAM(SendMessage(toolbar, TB_GETBUTTONSIZE, 0, 0));
        top += max(0, (height - buttonHeight) / 2);
        height = min(height, buttonHeight);
    }
    SetWindowPos(toolbar, nullptr, 0, top, ideal.cx, height, SWP_NOZORDER | SWP_NOACTIVATE);

    // will succeed for folders and EXEs, and fail for regular files
    // TODO: delay load?
    item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&dropTarget));

    tooltip = checkLE(CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, GetModuleHandle(nullptr), nullptr));
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (captionFont)
        SendMessage(tooltip, WM_SETFONT, (WPARAM)captionFont, FALSE);

    TOOLINFO toolInfo = {sizeof(toolInfo)};
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
    toolInfo.hwnd = parent;
    toolInfo.uId = (UINT_PTR)toolbar;
    toolInfo.lpszText = title;
    SendMessage(tooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
}

void ProxyIcon::destroy() {
    if (imageList)
        checkLE(ImageList_Destroy(imageList));
}

bool ProxyIcon::isToolbarWindow(HWND hwnd) const {
    return toolbar && (hwnd == toolbar);
}

POINT ProxyIcon::getMenuPoint(HWND parent) {
    if (toolbar) {
        RECT buttonRect;
        SendMessage(toolbar, TB_GETRECT, IDM_PROXY_BUTTON, (LPARAM)&buttonRect);
        return clientToScreen(toolbar, {buttonRect.left, buttonRect.bottom});
    } else {
        return clientToScreen(parent, {0, 0});
    }
}

void ProxyIcon::setItem(IShellItem *item) {
    if (toolbar) {
        dropTarget = nullptr;
        item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&dropTarget));
    }
}

void ProxyIcon::setTitle(wchar_t *title) {
    if (toolbar) {
        TBBUTTONINFO buttonInfo = {sizeof(buttonInfo)};
        buttonInfo.dwMask = TBIF_TEXT;
        buttonInfo.pszText = title;
        SendMessage(toolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);
    }
    if (tooltip) {
        TOOLINFO toolInfo = {sizeof(toolInfo)};
        toolInfo.hwnd = outer->hwnd;
        toolInfo.uId = (UINT_PTR)toolbar;
        toolInfo.lpszText = title;
        SendMessage(tooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
    }
}

void ProxyIcon::setIcon(HICON icon) {
    if (toolbar) {
        int iconSize = GetSystemMetrics(SM_CXSMICON);
        if (imageList)
            checkLE(ImageList_Destroy(imageList));
        imageList = ImageList_Create(iconSize, iconSize, ILC_MASK | ILC_COLOR32, 1, 0);
        ImageList_AddIcon(imageList, icon);
        SendMessage(toolbar, TB_SETIMAGELIST, 0, (LPARAM)imageList);
    }
}

void ProxyIcon::setActive(bool active) {
    if (toolbar && IsWindows8OrGreater())
        SetLayeredWindowAttributes(toolbar, 0, active ? 255 : INACTIVE_CAPTION_ALPHA, LWA_ALPHA);
}

void ProxyIcon::setPressedState(bool pressed) {
    if (toolbar) {
        LONG_PTR state = SendMessage(toolbar, TB_GETSTATE, IDM_PROXY_BUTTON, 0);
        state = pressed ? (state | TBSTATE_PRESSED) : (state & ~TBSTATE_PRESSED);
        SendMessage(toolbar, TB_SETSTATE, IDM_PROXY_BUTTON, state);
    }
}

void ProxyIcon::autoSize(LONG parentWidth, LONG captionLeft, LONG captionRight) {
    if (!toolbar)
        return;

    // turn on autosize to calculate the ideal width
    TBBUTTONINFO buttonInfo = {sizeof(buttonInfo)};
    buttonInfo.dwMask = TBIF_STYLE;
    buttonInfo.fsStyle = PROXY_BUTTON_STYLE | BTNS_AUTOSIZE;
    SendMessage(toolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);
    SIZE ideal;
    SendMessage(toolbar, TB_GETIDEALSIZE, FALSE, (LPARAM)&ideal);
    int actualLeft;
    if (outer->centeredProxy()) {
        int idealLeft = (parentWidth - ideal.cx) / 2;
        actualLeft = max(captionLeft, idealLeft);
    } else {
        actualLeft = captionLeft; // cover actual window title/icon
    }
    int maxWidth = parentWidth - actualLeft - captionRight;
    int actualWidth = min(ideal.cx, maxWidth);

    // turn off autosize to set exact width
    buttonInfo.dwMask = TBIF_STYLE | TBIF_SIZE;
    buttonInfo.fsStyle = PROXY_BUTTON_STYLE;
    buttonInfo.cx = (WORD)actualWidth;
    SendMessage(toolbar, TB_SETBUTTONINFO, IDM_PROXY_BUTTON, (LPARAM)&buttonInfo);

    RECT rect = windowRect(toolbar);
    MapWindowRect(nullptr, outer->hwnd, &rect);
    SetWindowPos(toolbar, nullptr, actualLeft, rect.top,
        actualWidth, rectHeight(rect), SWP_NOZORDER | SWP_NOACTIVATE);

    // show/hide tooltip if text truncated
    if (tooltip)
        SendMessage(tooltip, TTM_ACTIVATE, ideal.cx > maxWidth, 0);
}

void ProxyIcon::redrawToolbar() {
    if (toolbar)
        RedrawWindow(toolbar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void ProxyIcon::dragDrop(IDataObject *dataObject, POINT offset) {
    if (toolbar) {
        CComPtr<IDragSourceHelper> dragHelper;
        if (checkHR(dragHelper.CoCreateInstance(CLSID_DragDropHelper)))
            dragHelper->InitializeFromWindow(toolbar, &offset, dataObject);
    }

    DWORD okEffects = DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE;
    DWORD effect;
    dragging = true;
    // effect is supposed to be set to DROPEFFECT_MOVE if the target was unable to delete the
    // original, however the only time I could trigger this was moving a file into a ZIP folder,
    // which does successfully delete the original, only with a delay. So handling this as intended
    // would actually break dragging into ZIP folders and cause loss of data!
    // TODO: get CFSTR_PERFORMEDDROPEFFECT instead?
    if (DoDragDrop(dataObject, this, okEffects, &effect) == DRAGDROP_S_DROP) {
        // make sure the item is updated to the new path!
        // TODO: is this still necessary with SHChangeNotify?
        outer->resolveItem();
    }
    dragging = false;
}

RECT ProxyIcon::titleRect() {
    RECT rect;
    SendMessage(toolbar, TB_GETRECT, IDM_PROXY_BUTTON, (LPARAM)&rect);
    // hardcoded nonsense
    SIZE offset = IsThemeActive() ? SIZE{7, 5} : SIZE{3, 2};
    InflateRect(&rect, -PROXY_INFLATE.cx - offset.cx, -PROXY_INFLATE.cy - offset.cy);
    int iconSize = GetSystemMetrics(SM_CXSMICON);
    rect.left += iconSize;
    MapWindowRect(toolbar, outer->hwnd, &rect);
    return rect;
}

void ProxyIcon::beginRename() {
    if (!toolbar)
        return;
    CComHeapPtr<wchar_t> editingName;
    if (!checkHR(outer->item->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &editingName)))
        return;

    if (!renameBox) {
        // create rename box
        renameBox = checkLE(CreateWindow(L"EDIT", nullptr, WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, outer->hwnd, nullptr, GetModuleHandle(nullptr), nullptr));
        // support ctrl+backspace
        checkHR(SHAutoComplete(renameBox, SHACF_AUTOAPPEND_FORCE_OFF|SHACF_AUTOSUGGEST_FORCE_OFF));
        SetWindowSubclass(renameBox, renameBoxProc, 0, (DWORD_PTR)this);
        if (captionFont)
            SendMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, FALSE);
    }

    // update rename box rect
    int leftMargin = LOWORD(SendMessage(renameBox, EM_GETMARGINS, 0, 0));
    RECT textRect = titleRect();
    int renameHeight = rectHeight(textRect) + 4; // NOT scaled with DPI
    POINT renamePos = {textRect.left - leftMargin - 2,
                       (ItemWindow::CAPTION_HEIGHT - renameHeight) / 2};
    TITLEBARINFOEX titleBar = {sizeof(titleBar)};
    SendMessage(outer->hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&titleBar);
    int renameWidth = clientSize(outer->hwnd).cx - rectWidth(titleBar.rgrect[5]) - renamePos.x;
    renamePos = clientToScreen(outer->hwnd, renamePos);
    MoveWindow(renameBox, renamePos.x, renamePos.y, renameWidth, renameHeight, FALSE);

    SendMessage(renameBox, WM_SETTEXT, 0, (LPARAM)&*editingName);
    wchar_t *ext = PathFindExtension(editingName);
    if (ext == editingName) { // files that start with a dot
        Edit_SetSel(renameBox, 0, -1);
    } else {
        Edit_SetSel(renameBox, 0, ext - editingName);
    }
    ShowWindow(renameBox, SW_SHOW);
    EnableWindow(toolbar, FALSE);
}

bool ProxyIcon::isRenaming() {
    return renameBox && IsWindowVisible(renameBox);
}

void ProxyIcon::completeRename() {
    wchar_t newName[MAX_PATH];
    SendMessage(renameBox, WM_GETTEXT, _countof(newName), (LPARAM)newName);
    cancelRename();

    CComHeapPtr<wchar_t> editingName;
    if (!checkHR(outer->item->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &editingName)))
        return;

    if (lstrcmp(newName, editingName) == 0)
        return; // names are identical, which would cause an unnecessary error message
    if (PathCleanupSpec(nullptr, newName) & (PCS_REPLACEDCHAR | PCS_REMOVEDCHAR)) {
        outer->enableChain(false);
        checkHR(TaskDialog(outer->hwnd, GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDS_ERROR_CAPTION), nullptr, MAKEINTRESOURCE(IDS_INVALID_CHARS),
            TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr));
        outer->enableChain(true);
        return;
    }

    SHELLFLAGSTATE shFlags = {};
    SHGetSettings(&shFlags, SSF_SHOWEXTENSIONS);
    if (!shFlags.fShowExtensions) {
        CComQIPtr<IShellItem2> item2(outer->item);
        CComHeapPtr<wchar_t> display, ext;
        if (item2 && checkHR(item2->GetString(PKEY_ItemNameDisplay, &display)) && display
                && checkHR(item2->GetString(PKEY_FileExtension, &ext)) && ext) {
            if (lstrcmpi(display, editingName) != 0) {
                // extension was probably hidden (TODO: jank!)
                debugPrintf(L"Appending extension %s\n", &*ext);
                if (!checkHR(StringCchCat(newName, _countof(newName), ext)))
                    return;
            }
        }
    }

    outer->proxyRename(newName);
}

void ProxyIcon::cancelRename() {
    ShowWindow(renameBox, SW_HIDE);
    EnableWindow(toolbar, TRUE);
}

LRESULT CALLBACK ProxyIcon::renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_CHAR && wParam == VK_RETURN) {
        ((ProxyIcon *)refData)->completeRename();
        return 0;
    } else if (message == WM_CHAR && wParam == VK_ESCAPE) {
        ((ProxyIcon *)refData)->cancelRename();
        return 0;
    } else if (message == WM_CLOSE) {
        // prevent user closing rename box (it will still be destroyed when owner is closed)
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

bool ProxyIcon::onControlCommand(HWND controlHwnd, WORD notif) {
    if (renameBox && controlHwnd == renameBox && notif == EN_KILLFOCUS) {
        if (isRenaming())
            completeRename();
        return true;
    }
    return false;
}

LRESULT ProxyIcon::onNotify(NMHDR *nmHdr) {
    if (tooltip && nmHdr->hwndFrom == tooltip && nmHdr->code == TTN_SHOW) {
        // position tooltip on top of title
        RECT tooltipRect = titleRect();
        MapWindowRect(outer->hwnd, nullptr, &tooltipRect);
        SendMessage(tooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&tooltipRect);
        SetWindowPos(tooltip, nullptr, tooltipRect.left, tooltipRect.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    } else if (isToolbarWindow(nmHdr->hwndFrom) && nmHdr->code == NM_LDOWN) {
        if (isRenaming())
            return FALSE; // don't steal focus
        NMMOUSE *mouse = (NMMOUSE *)nmHdr;
        POINT screenPos = clientToScreen(toolbar, mouse->pt);
        if (DragDetect(toolbar, screenPos)) {
            POINT newCursorPos = {};
            GetCursorPos(&newCursorPos);
            // detect click-and-hold
            // https://devblogs.microsoft.com/oldnewthing/20100304-00/?p=14733
            RECT dragRect = {screenPos.x, screenPos.y, screenPos.x, screenPos.y};
            InflateRect(&dragRect, GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG));
            if (PtInRect(&dragRect, newCursorPos)
                    || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) {
                RECT toolbarRect = windowRect(toolbar);
                outer->proxyDrag({screenPos.x - toolbarRect.left, screenPos.y - toolbarRect.top});
            } else {
                outer->activate(); // wasn't activated due to handling WM_MOUSEACTIVATE
                outer->fakeDragMove();
                return TRUE;
            }
        } else {
            outer->activate(); // wasn't activated due to handling WM_MOUSEACTIVATE
        }
        return FALSE;
    } else if (isToolbarWindow(nmHdr->hwndFrom) && nmHdr->code == NM_CLICK) {
        // actually a double-click, since we captured the mouse in the NM_LDOWN handler (???)
        if (isRenaming())
            return FALSE;
        else if (GetKeyState(VK_MENU) < 0)
            outer->openProxyProperties();
        else
            outer->invokeProxyDefaultVerb();
        return TRUE;
    } else if (isToolbarWindow(nmHdr->hwndFrom) && nmHdr->code == NM_RCLICK) {
        setPressedState(true);
        outer->openProxyContextMenu();
        setPressedState(false);
        return TRUE;
    } else if (isToolbarWindow(nmHdr->hwndFrom) && nmHdr->code == TBN_GETOBJECT) {
        NMOBJECTNOTIFY *objNotif = (NMOBJECTNOTIFY *)nmHdr;
        if (dropTarget || dragging) {
            objNotif->pObject = (IDropTarget *)this;
            objNotif->hResult = S_OK;
            AddRef();
        } else {
            objNotif->pObject = nullptr;
            objNotif->hResult = E_FAIL;
        }
        return 0;
    }
    return 0;
}

void ProxyIcon::onThemeChanged() {
    if (toolbar && captionFont)
        PostMessage(toolbar, WM_SETFONT, (WPARAM)captionFont, TRUE);
    if (tooltip && captionFont)
        PostMessage(tooltip, WM_SETFONT, (WPARAM)captionFont, TRUE);
    if (renameBox && captionFont)
        PostMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, TRUE);
}

/* IUnknown */

STDMETHODIMP ProxyIcon::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        QITABENT(ProxyIcon, IDropSource),
        QITABENT(ProxyIcon, IDropTarget),
        {}
    };
    return QISearch(this, interfaces, id, obj);
}

STDMETHODIMP_(ULONG) ProxyIcon::AddRef() { return outer->AddRef(); }

STDMETHODIMP_(ULONG) ProxyIcon::Release() { return outer->Release(); }

/* IDropSource */

STDMETHODIMP ProxyIcon::QueryContinueDrag(BOOL escapePressed, DWORD keyState) {
    if (escapePressed)
        return DRAGDROP_S_CANCEL;
    if (!(keyState & (MK_LBUTTON | MK_RBUTTON)))
        return DRAGDROP_S_DROP;
    return S_OK;
}

STDMETHODIMP ProxyIcon::GiveFeedback(DWORD) {
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

/* IDropTarget */

static DWORD getDropEffect(DWORD keyState) {
    bool ctrl = (keyState & MK_CONTROL), shift = (keyState & MK_SHIFT), alt = (keyState & MK_ALT);
    if ((ctrl && shift && !alt) || (!ctrl && !shift && alt))
        return DROPEFFECT_LINK;
    else if (ctrl && !shift && !alt)
        return DROPEFFECT_COPY;
    else
        return DROPEFFECT_MOVE;
}

STDMETHODIMP ProxyIcon::DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt,
        DWORD *effect) {
    if (dragging) {
        *effect = getDropEffect(keyState); // pretend we can drop so the drag image is visible
    } else {
        if (!dropTarget || !checkHR(dropTarget->DragEnter(dataObject, keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (!dropTargetHelper)
        checkHR(dropTargetHelper.CoCreateInstance(CLSID_DragDropHelper));
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragEnter(toolbar, dataObject, &point, *effect));
    return S_OK;
}

STDMETHODIMP ProxyIcon::DragLeave() {
    if (!dragging) {
        if (!dropTarget || !checkHR(dropTarget->DragLeave()))
            return E_FAIL;
    }
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragLeave());
    return S_OK;
}

STDMETHODIMP ProxyIcon::DragOver(DWORD keyState, POINTL pt, DWORD *effect) {
    if (dragging) {
        *effect = getDropEffect(keyState);
    } else {
        if (!dropTarget || !checkHR(dropTarget->DragOver(keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (dropTargetHelper)
        checkHR(dropTargetHelper->DragOver(&point, *effect));
    return S_OK;
}

STDMETHODIMP ProxyIcon::Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) {
    if (dragging) {
        *effect = DROPEFFECT_NONE; // can't drop item onto itself
    } else {
        if (!dropTarget || !checkHR(dropTarget->Drop(dataObject, keyState, pt, effect)))
            return E_FAIL;
    }
    POINT point {pt.x, pt.y};
    if (dropTargetHelper)
        checkHR(dropTargetHelper->Drop(dataObject, &point, *effect));
    return S_OK;
}

} // namespace
