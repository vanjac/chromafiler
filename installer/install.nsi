!include LogicLib.nsh
!include MUI2.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\chromabrowse"
!define CONTEXT_MENU_TEXT "Open in chromabrowse"

Name "chromabrowse"
OutFile "..\build\chromabrowse-install.exe"
RequestExecutionLevel admin
Unicode True
SetCompressor LZMA

!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
	; InstallDirRegKey doesn't work with 64-bit view
	SetRegView 64
	ClearErrors
	ReadRegStr $INSTDIR HKLM "Software\chromabrowse" "Install_Dir"
	${If} ${Errors}
		StrCpy $INSTDIR "$PROGRAMFILES64\chromabrowse"
	${EndIf}
FunctionEnd

Section "chromabrowse"
	SectionIn RO
	SetOutPath $INSTDIR
	WriteUninstaller "$INSTDIR\uninstall.exe"
	SetRegView 64
	WriteRegStr HKLM Software\chromabrowse "Install_Dir" "$INSTDIR"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayName" "chromabrowse"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayIcon" "$INSTDIR\chromabrowse.exe,0"
	WriteRegStr HKLM "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegStr HKLM "${REG_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoModify" 1
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoRepair" 1
	File ..\build\chromabrowse.exe
SectionEnd

Section "Start Menu Shortcut"
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\chromabrowse.lnk" "$INSTDIR\chromabrowse.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\chromabrowse.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.browse"
SectionEnd

Section "Add to folder context menu" SecContext
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "none" ; change to "chromabrowse" to make default
	WriteRegStr HKCR CompressedFolder\Shell "" "none"
	WriteRegStr HKCR Drive\Shell "" "none"

	WriteRegStr HKCR Directory\shell\chromabrowse "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR Directory\Background\shell\chromabrowse "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR CompressedFolder\shell\chromabrowse "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR Drive\shell\chromabrowse "" "${CONTEXT_MENU_TEXT}"

	WriteRegStr HKCR Directory\shell\chromabrowse "Icon" "$INSTDIR\chromabrowse.exe"
	WriteRegStr HKCR Directory\Background\shell\chromabrowse "Icon" "$INSTDIR\chromabrowse.exe"
	WriteRegStr HKCR CompressedFolder\shell\chromabrowse "Icon" "$INSTDIR\chromabrowse.exe"
	WriteRegStr HKCR Drive\shell\chromabrowse "Icon" "$INSTDIR\chromabrowse.exe"

	Var /GLOBAL context_menu_command
	StrCpy $context_menu_command '"$INSTDIR\chromabrowse.exe" "%v"'
	WriteRegStr HKCR Directory\shell\chromabrowse\command "" '$context_menu_command'
	WriteRegStr HKCR Directory\Background\shell\chromabrowse\command "" '$context_menu_command'
	WriteRegStr HKCR CompressedFolder\shell\chromabrowse\command "" '$context_menu_command'
	WriteRegStr HKCR Drive\shell\chromabrowse\command "" '$context_menu_command'
SectionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	DeleteRegKey HKLM "${REG_UNINST_KEY}"
	DeleteRegKey HKLM Software\chromabrowse
	DeleteRegKey HKCR Directory\shell\chromabrowse
	DeleteRegKey HKCR Directory\Background\shell\chromabrowse
	DeleteRegKey HKCR CompressedFolder\shell\chromabrowse
	DeleteRegKey HKCR Drive\shell\chromabrowse
	Delete $SMPROGRAMS\chromabrowse.lnk
SectionEnd

LangString DESC_SecContext ${LANG_ENGLISH} "Add an 'Open in chromabrowse' command when right-clicking a folder."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_SecContext)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
