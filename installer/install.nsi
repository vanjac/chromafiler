Name "ChromaFiler"
OutFile "..\build\ChromaFiler-install.exe"
Unicode True
SetCompressor LZMA
!addplugindir plugins

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\ChromaFiler"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!define MULTIUSER_INSTALLMODE_INSTDIR "ChromaFiler"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\ChromaFiler"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "Install_Dir"
!include MultiUser.nsh

!include MUI2.nsh
!include EnumUsersReg.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChromaFiler"
!define CONTEXT_MENU_TEXT "Open in ChromaFiler"

!define MUI_ICON "..\src\res\folder.ico"
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION StartProgram

!getdllversion /productversion ..\build\ChromaFiler.exe PRODUCT_VERSION_
BrandingText "ChromaFiler v${PRODUCT_VERSION_1}.${PRODUCT_VERSION_2}.${PRODUCT_VERSION_3}"

!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
	SetRegView 64
	!insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
	SetRegView 64
	!insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "ChromaFiler" SecBase
	SectionIn RO
	SetOutPath $INSTDIR
	WriteUninstaller "$INSTDIR\uninstall.exe"
	SetRegView 64

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
SectionEnd

Section "Start Menu shortcut" SecStart
	; SMPROGRAMS will be set by MultiUser
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\ChromaFiler.lnk" "$INSTDIR\ChromaFiler.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\ChromaFiler.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.file"
SectionEnd

Section "Add to Open With menu" SecProgID
	SetRegView 64
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe\DefaultIcon" "" "C:\Windows\System32\imageres.dll,-102"
	WriteRegStr SHCTX "Software\Classes\Applications\ChromaFiler.exe\shell\open\command" "" '"$INSTDIR\ChromaFiler.exe" "%1"'
SectionEnd

Section "Add to folder context menu" SecContext
	SetRegView 64

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

	Var /GLOBAL context_menu_command
	StrCpy $context_menu_command '"$INSTDIR\ChromaFiler.exe" "%v"'
	WriteRegStr SHCTX Software\Classes\Directory\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\Directory\Background\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\CompressedFolder\shell\chromafiler\command "" '$context_menu_command'
	WriteRegStr SHCTX Software\Classes\Drive\shell\chromafiler\command "" '$context_menu_command'
SectionEnd

Function StartProgram
	; TODO will this work on non-English systems?
	InitPluginsDir
	File "/ONAME=$PLUGINSDIR\ShellExecAsUser.dll" "plugins\ShellExecAsUser.dll"
	CallAnsiPlugin::Call "$PLUGINSDIR\ShellExecAsUser.dll" ShellExecAsUser 2 "" '$INSTDIR\ChromaFiler.exe'
FunctionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	DeleteRegKey SHCTX "${REG_UNINST_KEY}"
	DeleteRegKey SHCTX Software\ChromaFiler
	DeleteRegKey SHCTX "Software\Classes\Applications\ChromaFiler.exe"
	DeleteRegKey SHCTX Software\Classes\Directory\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\Directory\Background\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\CompressedFolder\shell\chromafiler
	DeleteRegKey SHCTX Software\Classes\Drive\shell\chromafiler

	Delete $SMPROGRAMS\ChromaFiler.lnk

	${If} $MultiUser.InstallMode == "CurrentUser"
		Call un.CleanupCurrentUser
	${Else}
		${un.EnumUsersReg} un.CleanupUser chromafiler.temp
	${EndIf}
SectionEnd

Function un.CleanupCurrentUser
	DeleteRegKey HKCU "Software\ChromaFiler"
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
	DeleteRegKey HKU "$0\Software\ChromaFiler"
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

LangString DESC_SecBase ${LANG_ENGLISH} "The main application and required components."
LangString DESC_SecStart ${LANG_ENGLISH} "Add a shortcut to the Start Menu to launch ChromaFiler."
LangString DESC_SecProgID ${LANG_ENGLISH} "Add an entry to the 'Open with' menu for all file types. (Does not change the default app for any file type.)"
LangString DESC_SecContext ${LANG_ENGLISH} "Add an 'Open in ChromaFiler' command when right-clicking a folder."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_SecBase)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} $(DESC_SecStart)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecProgID} $(DESC_SecProgID)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_SecContext)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
