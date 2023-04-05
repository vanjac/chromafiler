#pragma once
#include <common.h>

#include "ItemWindow.h"
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
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    bool onCloseRequest() override;
    void onDestroy() override;
    bool onCommand(WORD command) override;
    bool onControlCommand(HWND controlHwnd, WORD notif) override;
    LRESULT onNotify(NMHDR *nmHdr) override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void addToolbarButtons(HWND tb) override;
    int getToolbarTooltip(WORD command) override;

private:
    const wchar_t * className() override;

    bool useDefaultStatusText() const override;
    SettingsPage settingsStartPage() const override;

    HWND createRichEdit(bool wordWrap);
    CComPtr<ITextDocument> getTOMDocument();
    void updateFont();
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

    HRESULT loadText();
    bool saveText();
    static DWORD CALLBACK streamOutCallback(DWORD_PTR cookie, LPBYTE buffer,
        LONG numBytes, LONG *bytesWritten);

    static LRESULT CALLBACK richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    enum Encoding {
        FAIL, UTF8, UTF8BOM, UTF16LE, UTF16BE
    };
    struct StreamInfo {
        CComPtr<IStream> stream;
        Encoding encoding;
    };

    HWND edit = nullptr;
    LOGFONT logFont; // NOT scaled for DPI
    HFONT font = nullptr; // scaled for DPI
    Encoding encoding = FAIL;
    bool isUnsavedScratchFile;
    int vScrollAccum = 0, hScrollAccum = 0; // for high resolution scrolling

    HWND findReplaceDialog = nullptr;
    FINDREPLACE findReplace;
    wchar_t findBuffer[128], replaceBuffer[128];
};

} // namespace
