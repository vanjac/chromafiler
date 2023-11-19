Name "ChromaFiler"
OutFile "..\build\ChromaFiler-install.exe"
Unicode True
SetCompressor LZMA
!addplugindir plugins

!define CHROMAFILER64 ; comment out for 32-bit

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!ifdef CHROMAFILER64
	!define MULTIUSER_USE_PROGRAMFILES64
!endif
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\ChromaFiler"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!define MULTIUSER_INSTALLMODE_INSTDIR "ChromaFiler"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\ChromaFiler"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "Install_Dir"
!define MULTIUSER_INSTALLMODEPAGE_TEXT_TOP "$(MULTIUSER_INNERTEXT_INSTALLMODE_TOP)$\r$\n$\r$\nIf you already have $(^Name) installed, do not change this setting."
!include MultiUser.nsh

!include MUI2.nsh
!include x64.nsh
!include EnumUsersReg.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChromaFiler"
!define REG_APPPATH_KEY "Software\Microsoft\Windows\CurrentVersion\App Paths\ChromaFiler.exe"
!define REG_TEXT_APPPATH_KEY "Software\Microsoft\Windows\CurrentVersion\App Paths\ChromaText.exe"
!define CONTEXT_MENU_TEXT "Open in ChromaFiler"
!define EXECUTE_GUID "{87612720-a94e-4fd3-a1f6-b78d7768424f}"
!define EXECUTE_TEXT_GUID "{14c46e3b-9015-4bbb-9f1f-9178a94f856f}"

!define MUI_ICON "..\src\res\folder.ico"
!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_LICENSEPAGE_TEXT_TOP ""
!define MUI_LICENSEPAGE_TEXT_BOTTOM "Click Next to continue."
!define MUI_LICENSEPAGE_BUTTON "&Next >"
!define MUI_TEXT_LICENSE_TITLE "Welcome to ChromaFiler Setup"
!define MUI_TEXT_LICENSE_SUBTITLE "Setup will guide you through the installation of ChromaFiler."

!define MUI_FINISHPAGE_RUN_TEXT "(Re)open the tray"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION OpenTray
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!getdllversion /productversion ..\build\ChromaFiler.exe PRODUCT_VERSION_
!ifdef CHROMAFILER64
	BrandingText "$(^Name) v${PRODUCT_VERSION_1}.${PRODUCT_VERSION_2}.${PRODUCT_VERSION_3} (64-bit)"
!else
	BrandingText "$(^Name) v${PRODUCT_VERSION_1}.${PRODUCT_VERSION_2}.${PRODUCT_VERSION_3} (32-bit)"
!endif

!insertmacro MUI_PAGE_LICENSE licensetext.txt
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page Custom LockedListShow
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishPageShow
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
UninstPage Custom un.LockedListShow
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

LangString DESC_SecBase ${LANG_ENGLISH} "The main application and required components."
LangString DESC_SecText ${LANG_ENGLISH} "Integrated Notepad-like text editor."
LangString DESC_SecStart ${LANG_ENGLISH} "Add a shortcut to the Start Menu to launch $(^Name)."
LangString DESC_SecProgID ${LANG_ENGLISH} "Add an entry to the 'Open with' menu for all file types. (Does not change the default app for any file type.)"
LangString DESC_SecContext ${LANG_ENGLISH} "Add 'Open in $(^Name)' command when right-clicking a folder. Required to set default file browser."
LangString DESC_SecRemoveSettings ${LANG_ENGLISH} "If unchecked, settings will be restored the next time you install ChromaFiler."
LangString LOCKED_LIST_TITLE ${LANG_ENGLISH} "Close Programs"
LangString LOCKED_LIST_SUBTITLE ${LANG_ENGLISH} "Make sure all $(^Name) windows are closed before continuing (including the tray)."


Function .onInit
	InitPluginsDir
!ifdef CHROMAFILER64
	${IfNot} ${RunningX64}
		MessageBox MB_ICONSTOP "This is the 64-bit version of ChromaFiler, but you're using 32-bit Windows. Please download the 32-bit installer."
		Quit
	${EndIf}
	SetRegView 64
	File /oname=$PLUGINSDIR\LockedList64.dll `plugins\LockedList64.dll`
!else
	${If} ${RunningX64}
		MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "This is the 32-bit version of ChromaFiler, but you're using 64-bit Windows. Installing the 64-bit version is recommended. Continue anyway?" IDYES continue32bit
		Quit
continue32bit:
	${EndIf}
	File /oname=$PLUGINSDIR\LockedList.dll `plugins\LockedList.dll`
!endif
	!insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
	InitPluginsDir
!ifdef CHROMAFILER64
	SetRegView 64
	File /oname=$PLUGINSDIR\LockedList64.dll `plugins\LockedList64.dll`
!else
	File /oname=$PLUGINSDIR\LockedList.dll `plugins\LockedList.dll`
!endif
	!insertmacro MULTIUSER_UNINIT
FunctionEnd

Var tray_window
!macro CUSTOM_LOCKEDLIST_PAGE
	FindWindow $tray_window "ChromaFile Tray"
	${If} $tray_window != 0
		System::Call "user32.dll::PostMessage(i $tray_window, i 16, i 0, i 0)"
	${EndIf}
	!insertmacro MUI_HEADER_TEXT $(LOCKED_LIST_TITLE) $(LOCKED_LIST_SUBTITLE)
	LockedList::AddModule "$INSTDIR\ChromaFiler.exe"
	LockedList::Dialog /autonext
	Pop $R0
!macroend

Function LockedListShow
	!insertmacro CUSTOM_LOCKEDLIST_PAGE
FunctionEnd

Function un.LockedListShow
	!insertmacro CUSTOM_LOCKEDLIST_PAGE
FunctionEnd

Function SilentSearchCallback
	Pop $R0
	Pop $R1
	Pop $R2
	${If} $R0 != -1
		Push autoclose
	${EndIf}
FunctionEnd

Section "ChromaFiler" SecBase
	SectionIn RO
	SetOutPath $INSTDIR

	${If} ${Silent}
		LockedList::AddModule "$INSTDIR\ChromaFiler.exe"
		GetFunctionAddress $R0 SilentSearchCallback
		LockedList::SilentSearch $R0
		Pop $R0
	${EndIf}

	WriteUninstaller "$INSTDIR\uninstall.exe"

	; MultiUser.nsh never actually writes this value
	WriteRegStr SHCTX "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY}" "${MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME}" $MultiUser.InstallMode

	WriteRegStr SHCTX Software\ChromaFiler "Install_Dir" "$INSTDIR"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "DisplayName" "ChromaFiler"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "DisplayIcon" "$INSTDIR\ChromaFiler.exe,0"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "Publisher" "chroma zone"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "VersionMajor" "${PRODUCT_VERSION_1}"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "VersionMinor" "${PRODUCT_VERSION_2}"
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegStr SHCTX "${REG_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
	WriteRegDWORD SHCTX "${REG_UNINST_KEY}" "NoModify" 1
	WriteRegDWORD SHCTX "${REG_UNINST_KEY}" "NoRepair" 1

	File ..\build\ChromaFiler.exe
	File /oname=LICENSE.txt ..\LICENSE
	; previous installers didn't initialize PLUGINSDIR correctly
	Delete $INSTDIR\LockedList64.dll
	Delete $INSTDIR\LockedList.dll

	; https://learn.microsoft.com/en-us/windows/win32/shell/app-registration
	WriteRegStr SHCTX "${REG_APPPATH_KEY}" "" "$INSTDIR\ChromaFiler.exe"
	WriteRegStr SHCTX "${REG_APPPATH_KEY}" "DropTarget" "${EXECUTE_GUID}"

	WriteRegStr SHCTX "Software\Classes\CLSID\${EXECUTE_GUID}" "" "ChromaFiler"
	WriteRegStr SHCTX "Software\Classes\CLSID\${EXECUTE_GUID}\LocalServer32" "" "$INSTDIR\ChromaFiler.exe"
SectionEnd

Section "ChromaText (text editor)" SecText
	File ..\build\ChromaText.exe

	WriteRegStr SHCTX "${REG_TEXT_APPPATH_KEY}" "" "$INSTDIR\ChromaText.exe"
	WriteRegStr SHCTX "${REG_TEXT_APPPATH_KEY}" "DropTarget" "${EXECUTE_TEXT_GUID}"

	WriteRegStr SHCTX "Software\Classes\CLSID\${EXECUTE_TEXT_GUID}" "" "ChromaText"
	WriteRegStr SHCTX "Software\Classes\CLSID\${EXECUTE_TEXT_GUID}\LocalServer32" "" "$INSTDIR\ChromaFiler.exe"
SectionEnd

Section "Start Menu shortcut" SecStart
	; SMPROGRAMS will be set by MultiUser
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\ChromaFiler.lnk" "$INSTDIR\ChromaFiler.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\ChromaFiler.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.file"
	${If} ${SectionIsSelected} ${SecText}
		Call CreateChromaTextShortcut
	${EndIf}
SectionEnd

Function CreateChromaTextShortcut
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\ChromaText.lnk" "$INSTDIR\ChromaText.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\ChromaText.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.text"
FunctionEnd

Section "Add to Open With menu" SecProgID
	; https://learn.microsoft.com/en-us/windows/win32/shell/customizing-file-types-bumper
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe" "FriendlyAppName" "ChromaFiler Preview"
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe" "AppUserModelID" "chroma.file"
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe\shell\open\command" "" '"$INSTDIR\ChromaFiler.exe" "%1"'
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe\shell\open\command" "DelegateExecute" "${EXECUTE_GUID}"
	; hack for ArsClip (TODO: add to all users?)
	WriteRegStr HKCU "Software\Classes\Applications\ChromaFiler.exe\shell\open\command" "" '"$INSTDIR\ChromaFiler.exe" "%1"'
	; clean up old icon (removed v0.7.0)
	DeleteRegKey SHCTX "Software\Classes\Applications\ChromaFiler.exe\DefaultIcon"

	${If} ${SectionIsSelected} ${SecText}
		; Application
		WriteRegStr SHCTX "Software\Classes\Applications\ChromaText.exe" "FriendlyAppName" "ChromaText"
		WriteRegStr SHCTX "Software\Classes\Applications\ChromaText.exe" "AppUserModelID" "chroma.text"
		WriteRegStr SHCTX "Software\Classes\Applications\ChromaText.exe" "NoOpenWith" ""
		WriteRegStr SHCTX "Software\Classes\Applications\ChromaText.exe\shell\open\command" "" '"$INSTDIR\ChromaText.exe" "%1"'
		WriteRegStr SHCTX "Software\Classes\Applications\ChromaText.exe\shell\open\command" "DelegateExecute" "${EXECUTE_TEXT_GUID}"
		; ProgID
		WriteRegStr SHCTX "Software\Classes\Chroma.Text" "" "ChromaText"
		WriteRegStr SHCTX "Software\Classes\Chroma.Text" "AppUserModelID" "chroma.text"
		WriteRegStr SHCTX "Software\Classes\Chroma.Text\DefaultIcon" "" "C:\Windows\System32\imageres.dll,-102"
		WriteRegStr SHCTX "Software\Classes\Chroma.Text\shell\open\command" "" '"$INSTDIR\ChromaText.exe" "%1"'
		WriteRegStr SHCTX "Software\Classes\Chroma.Text\shell\open\command" "DelegateExecute" "${EXECUTE_TEXT_GUID}"
		WriteRegStr SHCTX "Software\Classes\*\OpenWithProgids" "Chroma.Text" ""
		; preview handler
		WriteRegStr SHCTX "Software\Classes\Chroma.Text\shellex\{8895b1c6-b41f-4c1c-a562-0d564250836f}" "" "{1531d583-8375-4d3f-b5fb-d23bbd169f22}"
		; hack
		WriteRegStr HKCU "Software\Classes\Chroma.Text\shell\open\command" "" '"$INSTDIR\ChromaText.exe" "%1"'
	${EndIf}
SectionEnd

Section "Add to folder context menu" SecContext
	Var /GLOBAL default_browser
	; clear shell defaults if empty / not defined
	ReadRegStr $default_browser SHCTX "Software\Classes\Directory\Shell" ""
	StrCmp $default_browser "" 0 +2
		WriteRegStr SHCTX "Software\Classes\Directory\Shell" "" "none"
	ReadRegStr $default_browser SHCTX "Software\Classes\CompressedFolder\Shell" ""
	StrCmp $default_browser "" 0 +2
		WriteRegStr SHCTX "Software\Classes\CompressedFolder\Shell" "" "none"
	ReadRegStr $default_browser SHCTX "Software\Classes\Drive\Shell" ""
	StrCmp $default_browser "" 0 +2
		WriteRegStr SHCTX "Software\Classes\Drive\Shell" "" "none"

	WriteRegStr SHCTX Software\Classes\Directory\shell\chromafiler "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr SHCTX Software\Classes\Directory\Background\shell\chromafiler "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr SHCTX Software\Classes\CompressedFolder\shell\chromafiler "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr SHCTX Software\Classes\Drive\shell\chromafiler "" "${CONTEXT_MENU_TEXT}"

	WriteRegStr SHCTX Software\Classes\Directory\shell\chromafiler "Icon" "$INSTDIR\ChromaFiler.exe"
	WriteRegStr SHCTX Software\Classes\Directory\Background\shell\chromafiler "Icon" "$INSTDIR\ChromaFiler.exe"
	WriteRegStr SHCTX Software\Classes\CompressedFolder\shell\chromafiler "Icon" "$INSTDIR\ChromaFiler.exe"
	WriteRegStr SHCTX Software\Classes\Drive\shell\chromafiler "Icon" "$INSTDIR\ChromaFiler.exe"

	; https://superuser.com/questions/136838/which-special-variables-are-available-when-writing-a-shell-command-for-a-context
	; explorer uses: %SystemRoot%\Explorer.exe /idlist,%I,%L
	Var /GLOBAL context_menu_command
	StrCpy $context_menu_command '"$INSTDIR\ChromaFiler.exe" "%v"'
	WriteRegStr SHCTX Software\Classes\Directory\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\Directory\Background\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\CompressedFolder\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\Drive\shell\chromafiler\command "" '$context_menu_command'

	WriteRegStr SHCTX Software\Classes\Directory\shell\chromafiler\command "DelegateExecute" "${EXECUTE_GUID}"
	WriteRegStr SHCTX Software\Classes\Directory\Background\shell\chromafiler\command "DelegateExecute" "${EXECUTE_GUID}"
	WriteRegStr SHCTX Software\Classes\CompressedFolder\shell\chromafiler\command "DelegateExecute" "${EXECUTE_GUID}"
	WriteRegStr SHCTX Software\Classes\Drive\shell\chromafiler\command "DelegateExecute" "${EXECUTE_GUID}"
SectionEnd

Function FinishPageShow
	${If} $tray_window != 0
		SendMessage $mui.FinishPage.Run ${BM_SETCHECK} ${BST_CHECKED} 0
	${EndIf}
FunctionEnd

Function OpenTray
	; https://mdb-blog.blogspot.com/2013/01/nsis-lunch-program-as-user-from-uac.html
	CreateShortCut "$TEMP\ChromaFilerTray.lnk" "$INSTDIR\ChromaFiler.exe" "/tray"
	Exec '"$WINDIR\explorer.exe" "$TEMP\ChromaFilerTray.lnk"'
FunctionEnd

Section "un.Uninstall ChromaFiler"
	SectionIn RO

	Delete $INSTDIR\*.exe
	Delete $INSTDIR\LICENSE.txt
	RMDir $INSTDIR
	DeleteRegKey SHCTX "${REG_UNINST_KEY}"
	DeleteRegKey SHCTX "${REG_APPPATH_KEY}"
	DeleteRegKey SHCTX "${REG_TEXT_APPPATH_KEY}"
	DeleteRegKey SHCTX "Software\Classes\Applications\ChromaFiler.exe"
	DeleteRegKey SHCTX "Software\Classes\Applications\ChromaText.exe"
	DeleteRegKey SHCTX "Software\Classes\Chroma.Text"
	DeleteRegValue SHCTX "Software\Classes\*\OpenWithProgids" "Chroma.Text"
	DeleteRegKey SHCTX Software\Classes\Directory\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\Directory\Background\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\CompressedFolder\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\Drive\shell\chromafiler
	DeleteRegKey SHCTX "Software\Classes\CLSID\${EXECUTE_GUID}"
	DeleteRegKey SHCTX "Software\Classes\CLSID\${EXECUTE_TEXT_GUID}"

	Delete $SMPROGRAMS\ChromaFiler.lnk
	Delete $SMPROGRAMS\ChromaText.lnk

	; Clean up hacks
	DeleteRegKey HKCU "Software\Classes\Applications\ChromaFiler.exe"
	DeleteRegKey HKCU "Software\Classes\Chroma.Text"

	; Clean up settings written by ChromaFiler (could be under multiple users)
	${If} $MultiUser.InstallMode == "CurrentUser"
		Call un.CleanupCurrentUser
	${Else}
		${un.EnumUsersReg} un.CleanupUser chromafiler.temp
	${EndIf}
SectionEnd

Section "un.Remove all settings" SecRemoveSettings
	DeleteRegKey SHCTX Software\ChromaFiler
	DeleteRegKey HKCU "Software\ChromaFiler"
	; Other users' settings will not be erased
SectionEnd

Function un.CleanupCurrentUser
	DeleteRegValue HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "ChromaFiler"

	; clear shell defaults if set to chromafiler
	ReadRegStr $default_browser HKCU "Software\Classes\Directory\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKCU "Software\Classes\Directory\Shell" "" "none"
	ReadRegStr $default_browser HKCU "Software\Classes\CompressedFolder\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKCU "Software\Classes\CompressedFolder\Shell" "" "none"
	ReadRegStr $default_browser HKCU "Software\Classes\Drive\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKCU "Software\Classes\Drive\Shell" "" "none"
FunctionEnd

Function un.CleanupUser
	Pop $0
	DeleteRegValue HKU "$0\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "ChromaFiler"

	; clear shell defaults if set to chromafiler
	ReadRegStr $default_browser HKU "$0\Software\Classes\Directory\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKU "$0\Software\Classes\Directory\Shell" "" "none"
	ReadRegStr $default_browser HKU "$0\Software\Classes\CompressedFolder\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKU "$0\Software\Classes\CompressedFolder\Shell" "" "none"
	ReadRegStr $default_browser HKU "$0\Software\Classes\Drive\Shell" ""
	StrCmp $default_browser "chromafiler" 0 +2
		WriteRegStr HKU "$0\Software\Classes\Drive\Shell" "" "none"
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_SecBase)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecText} $(DESC_SecText)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} $(DESC_SecStart)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecProgID} $(DESC_SecProgID)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_SecContext)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecRemoveSettings} $(DESC_SecRemoveSettings)
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
