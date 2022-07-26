Name "chromabrowse"
OutFile "..\build\chromabrowse-install.exe"
RequestExecutionLevel admin
Unicode True
SetCompressor LZMA

!include MUI2.nsh
!include EnumUsersReg.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\chromabrowse"
!define CONTEXT_MENU_TEXT "Open in chromabrowse"

!define MUI_COMPONENTSPAGE_SMALLDESC

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
	ReadRegStr $INSTDIR HKLM "Software\chromabrowse" "Install_Dir"
	${If} ${Errors}
		StrCpy $INSTDIR "$PROGRAMFILES64\chromabrowse"
	${EndIf}
FunctionEnd

Section "chromabrowse" SecBase
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

	; Clean up previous versions
	DeleteRegValue HKLM SOFTWARE\Microsoft\Windows\CurrentVersion\Run "chromabrowse"
SectionEnd

Section "Start Menu Shortcut" SecStart
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\chromabrowse.lnk" "$INSTDIR\chromabrowse.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\chromabrowse.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.browse"
SectionEnd

Section "Add to folder context menu" SecContext
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "none"
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

Section /o "    Make default file browser (experimental!)" SecDefault
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "chromabrowse"
	WriteRegStr HKCR CompressedFolder\Shell "" "chromabrowse"
	WriteRegStr HKCR Drive\Shell "" "chromabrowse"
SectionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "none"
	WriteRegStr HKCR CompressedFolder\Shell "" "none"
	WriteRegStr HKCR Drive\Shell "" "none"
	DeleteRegKey HKLM "${REG_UNINST_KEY}"
	DeleteRegKey HKLM Software\chromabrowse
	DeleteRegKey HKCR Directory\shell\chromabrowse
	DeleteRegKey HKCR Directory\Background\shell\chromabrowse
	DeleteRegKey HKCR CompressedFolder\shell\chromabrowse
	DeleteRegKey HKCR Drive\shell\chromabrowse
	Delete $SMPROGRAMS\chromabrowse.lnk

	${un.EnumUsersReg} un.CleanupUser chromabrowse.temp
SectionEnd

Function un.CleanupUser
	Pop $0
	DeleteRegKey HKU "$0\Software\chromabrowse"
	DeleteRegValue HKU "$0\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "chromabrowse"
FunctionEnd

LangString DESC_SecBase ${LANG_ENGLISH} "The main application and required components."
LangString DESC_SecStart ${LANG_ENGLISH} "Add a shortcut to the start menu to launch chromabrowse."
LangString DESC_SecContext ${LANG_ENGLISH} "Add an 'Open in chromabrowse' command when right-clicking a folder."
LangString DESC_SecDefault ${LANG_ENGLISH} "Replace File Explorer as the default program for opening folders. Use at your own risk!"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_SecBase)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} $(DESC_SecStart)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_SecContext)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecDefault} $(DESC_SecDefault)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onSelChange
	; SecDefault depends on SecContext
	${IfNot} ${SectionIsSelected} ${SecContext}
		!insertmacro SetSectionFlag ${SecDefault} ${SF_RO}
		!insertmacro UnselectSection ${SecDefault}
	${Else}
		!insertmacro ClearSectionFlag ${SecDefault} ${SF_RO}
	${EndIf}
FunctionEnd
