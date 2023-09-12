#pragma once
#include <common.h>

#include "ItemWindow.h"
#include "Settings.h"
#include <Richedit.h>
#include <commdlg.h>
#include <TOM.h>

namespace chromafiler {

class TextWindow : public ItemWindow {
public:
    static void init();

    TextWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item, bool scratch = false);

    static void updateAllSettings();

    bool handleTopLevelMessage(MSG *msg) override;

protected:
    enum UserMessage {
        // WPARAM: 0, LPARAM: 0
        MSG_LOAD_COMPLETE = ItemWindow::MSG_LAST,
        // WPARAM: HRESULT, LPARAM: 0
        MSG_LOAD_FAIL,
        MSG_LAST
    };
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    const wchar_t * appUserModelID() const override;
    bool useDefaultStatusText() const override;
    SettingsPage settingsStartPage() const override;
    const wchar_t * helpURL() const override;

    void updateWindowPropStore(CComPtr<IPropertyStore> propStore) override;

    void resetPropBag(CComPtr<IPropertyBag> bag) override;

    void onCreate() override;
    bool onCloseRequest() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    bool onControlCommand(HWND controlHwnd, WORD notif) override;
    LRESULT onNotify(NMHDR *nmHdr) override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(SIZE size) override;

    void addToolbarButtons(HWND tb) override;
    int getToolbarTooltip(WORD command) override;

    void trackContextMenu(POINT pos) override;

private:
    const wchar_t * className() const override;

    HWND createRichEdit(bool readOnly, bool wordWrap);
    bool isEditable();
    CComPtr<ITextDocument> getTOMDocument();
    void updateFont();
    void updateEditSize();
    void updateStatus();
    void userSave();
    bool confirmSave(bool willDelete);

    void changeFontSize(int amount);
    bool isWordWrap();
    void setWordWrap(bool wordWrap);
    void newLine();
    void indentSelection(int dir); // dir can be 1 (right) or -1 (left)
    void lineSelect();
    void openFindDialog(bool replace);
    void handleFindReplace(FINDREPLACE *input);
    void findNext(FINDREPLACE *input);
    void replace(FINDREPLACE *input);
    int replaceAll(FINDREPLACE *input);

    struct LoadResult {
        std::unique_ptr<uint8_t[]> buffer; // null terminated!
        uint8_t *textStart;
        SETTEXTEX setText;
        TextEncoding encoding;
        TextNewlines newlines;
    };

    static HRESULT loadText(CComPtr<IShellItem> item, LoadResult *result);
    HRESULT saveText();

    static LRESULT CALLBACK richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    HWND edit = nullptr;
    LOGFONT logFont; // NOT scaled for DPI
    HFONT font = nullptr; // scaled for DPI
    TextEncoding detectEncoding = ENC_UNK;
    TextNewlines detectNewlines = NL_UNK;
    bool isUnsavedScratchFile;
    int vScrollAccum = 0, hScrollAccum = 0; // for high resolution scrolling

    HWND findReplaceDialog = nullptr;
    FINDREPLACE findReplace;
    wchar_t findBuffer[128], replaceBuffer[128];

    SRWLOCK asyncLoadResultLock = SRWLOCK_INIT;
    LoadResult asyncLoadResult;

    class LoadThread : public StoppableThread {
    public:
        LoadThread(CComPtr<IShellItem> item, TextWindow *callbackWindow);
    protected:
        void run() override;
    private:
        CComHeapPtr<ITEMIDLIST> itemIDList;
        TextWindow *callbackWindow;
    };
    CComPtr<LoadThread> loadThread;
};

} // namespace
