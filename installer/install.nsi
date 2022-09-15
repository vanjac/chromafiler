Name "chromafile"
OutFile "..\build\chromafile-install.exe"
RequestExecutionLevel admin
Unicode True
SetCompressor LZMA
!addplugindir plugins

!include MUI2.nsh
!include EnumUsersReg.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\chromafile"
!define CONTEXT_MENU_TEXT "Open in chromafile"

!define MUI_ICON "..\src\res\folder.ico"
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION StartProgram

!getdllversion /productversion ..\build\chromafile.exe PRODUCT_VERSION_
BrandingText "chromafile v${PRODUCT_VERSION_1}.${PRODUCT_VERSION_2}.${PRODUCT_VERSION_3}"

!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
	; InstallDirRegKey doesn't work with 64-bit view
	SetRegView 64
	ClearErrors
	ReadRegStr $INSTDIR HKLM "Software\chromafile" "Install_Dir"
	${If} ${Errors}
		StrCpy $INSTDIR "$PROGRAMFILES64\chromafile"
	${EndIf}
FunctionEnd

Section "chromafile" SecBase
	SectionIn RO
	SetOutPath $INSTDIR
	WriteUninstaller "$INSTDIR\uninstall.exe"
	SetRegView 64
	WriteRegStr HKLM Software\chromafile "Install_Dir" "$INSTDIR"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayName" "chromafile"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayIcon" "$INSTDIR\chromafile.exe,0"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "Publisher" "chroma zone"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "VersionMajor" "${PRODUCT_VERSION_1}"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "VersionMinor" "${PRODUCT_VERSION_2}"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegStr HKLM "${REG_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoModify" 1
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoRepair" 1
	File ..\build\chromafile.exe

	;;; clean up after previous versions that wrote to HKCR instead of HKCU/HKLM ;;;

	DeleteRegKey HKCU "Software\Classes\Applications\chromafile.exe"
	DeleteRegKey HKCU Software\Classes\Directory\shell\chromafile
	DeleteRegKey HKCU Software\Classes\Directory\Background\shell\chromafile
	DeleteRegKey HKCU Software\Classes\CompressedFolder\shell\chromafile
	DeleteRegKey HKCU Software\Classes\Drive\shell\chromafile

	Var /GLOBAL default_browser
	ReadRegStr $default_browser HKLM Software\Classes\Directory\Shell ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKLM Software\Classes\Directory\Shell "" "none"
	ReadRegStr $default_browser HKLM Software\Classes\CompressedFolder\Shell ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKLM Software\Classes\CompressedFolder\Shell "" "none"
	ReadRegStr $default_browser HKLM Software\Classes\Drive\Shell ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKLM Software\Classes\Drive\Shell "" "none"
SectionEnd

Section "Start Menu shortcut" SecStart
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\chromafile.lnk" "$INSTDIR\chromafile.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\chromafile.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.file"
SectionEnd

Section "Add to Open With menu" SecProgID
	SetRegView 64
	WriteRegStr HKLM "Software\Classes\Applications\chromafile.exe\DefaultIcon" "" "C:\Windows\System32\imageres.dll,-102"
	WriteRegStr HKLM "Software\Classes\Applications\chromafile.exe\shell\open\command" "" '"$INSTDIR\chromafile.exe" "%1"'
SectionEnd

Section "Add to folder context menu" SecContext
	SetRegView 64
	WriteRegStr HKLM Software\Classes\Directory\Shell "" "none"
	WriteRegStr HKLM Software\Classes\CompressedFolder\Shell "" "none"
	WriteRegStr HKLM Software\Classes\Drive\Shell "" "none"

	WriteRegStr HKLM Software\Classes\Directory\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKLM Software\Classes\Directory\Background\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKLM Software\Classes\CompressedFolder\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKLM Software\Classes\Drive\shell\chromafile "" "${CONTEXT_MENU_TEXT}"

	WriteRegStr HKLM Software\Classes\Directory\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKLM Software\Classes\Directory\Background\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKLM Software\Classes\CompressedFolder\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKLM Software\Classes\Drive\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"

	Var /GLOBAL context_menu_command
	StrCpy $context_menu_command '"$INSTDIR\chromafile.exe" "%v"'
	WriteRegStr HKLM Software\Classes\Directory\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKLM Software\Classes\Directory\Background\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKLM Software\Classes\CompressedFolder\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKLM Software\Classes\Drive\shell\chromafile\command "" '$context_menu_command'
SectionEnd

Function StartProgram
	InitPluginsDir
	File "/ONAME=$PLUGINSDIR\ShellExecAsUser.dll" "plugins\ShellExecAsUser.dll"
	CallAnsiPlugin::Call "$PLUGINSDIR\ShellExecAsUser.dll" ShellExecAsUser 2 "" '$INSTDIR\chromafile.exe'
FunctionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	DeleteRegKey HKLM "${REG_UNINST_KEY}"
	DeleteRegKey HKLM Software\chromafile
	DeleteRegKey HKLM "Software\Classes\Applications\chromafile.exe"
	DeleteRegKey HKLM Software\Classes\Directory\shell\chromafile
	DeleteRegKey HKLM Software\Classes\Directory\Background\shell\chromafile
	DeleteRegKey HKLM Software\Classes\CompressedFolder\shell\chromafile
	DeleteRegKey HKLM Software\Classes\Drive\shell\chromafile

	Delete $SMPROGRAMS\chromafile.lnk

	${un.EnumUsersReg} un.CleanupUser chromafile.temp
SectionEnd

Function un.CleanupUser
	Pop $0
	DeleteRegKey HKU "$0\Software\chromafile"
	DeleteRegValue HKU "$0\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "chromafile"

	ReadRegStr $default_browser HKU "$0\Software\Classes\Directory\Shell" ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKU "$0\Software\Classes\Directory\Shell" "" "none"
	ReadRegStr $default_browser HKU "$0\Software\Classes\CompressedFolder\Shell" ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKU "$0\Software\Classes\CompressedFolder\Shell" "" "none"
	ReadRegStr $default_browser HKU "$0\Software\Classes\Drive\Shell" ""
	StrCmp $default_browser "chromafile" 0 +2
		WriteRegStr HKU "$0\Software\Classes\Drive\Shell" "" "none"
FunctionEnd

LangString DESC_SecBase ${LANG_ENGLISH} "The main application and required components."
LangString DESC_SecStart ${LANG_ENGLISH} "Add a shortcut to the Start Menu to launch chromafile."
LangString DESC_SecProgID ${LANG_ENGLISH} "Add an entry to the 'Open with' menu for all file types. (Does not change the default app for any file type.)"
LangString DESC_SecContext ${LANG_ENGLISH} "Add an 'Open in chromafile' command when right-clicking a folder."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_SecBase)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} $(DESC_SecStart)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecProgID} $(DESC_SecProgID)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_SecContext)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
