Name "chromafile"
OutFile "..\build\chromafile-install.exe"
RequestExecutionLevel admin
Unicode True
SetCompressor LZMA

!include MUI2.nsh
!include EnumUsersReg.nsh
!include "nsis-shortcut-properties\shortcut-properties.nsh"

!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\chromafile"
!define CONTEXT_MENU_TEXT "Open in chromafile"

!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_FINISHPAGE_RUN "$INSTDIR\chromafile.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

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
	WriteRegStr HKLM "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegStr HKLM "${REG_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoModify" 1
	WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoRepair" 1
	File ..\build\chromafile.exe
SectionEnd

Section "Start Menu Shortcut" SecStart
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\chromafile.lnk" "$INSTDIR\chromafile.exe"
	!insertmacro ShortcutSetToastProperties "$SMPROGRAMS\chromafile.lnk" "{bcf1926f-5819-497a-93b6-dc2b165ddd9c}" "chroma.file"
SectionEnd

Section "Add to Open with menu" SecProgID
	SetRegView 64
	WriteRegStr HKCR "Applications\chromafile.exe\DefaultIcon" "" "C:\Windows\System32\imageres.dll,-102"
	WriteRegStr HKCR "Applications\chromafile.exe\shell\open\command" "" '"$INSTDIR\chromafile.exe" "%1"'
SectionEnd

Section "Add to folder context menu" SecContext
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "none"
	WriteRegStr HKCR CompressedFolder\Shell "" "none"
	WriteRegStr HKCR Drive\Shell "" "none"

	WriteRegStr HKCR Directory\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR Directory\Background\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR CompressedFolder\shell\chromafile "" "${CONTEXT_MENU_TEXT}"
	WriteRegStr HKCR Drive\shell\chromafile "" "${CONTEXT_MENU_TEXT}"

	WriteRegStr HKCR Directory\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKCR Directory\Background\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKCR CompressedFolder\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"
	WriteRegStr HKCR Drive\shell\chromafile "Icon" "$INSTDIR\chromafile.exe"

	Var /GLOBAL context_menu_command
	StrCpy $context_menu_command '"$INSTDIR\chromafile.exe" "%v"'
	WriteRegStr HKCR Directory\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKCR Directory\Background\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKCR CompressedFolder\shell\chromafile\command "" '$context_menu_command'
	WriteRegStr HKCR Drive\shell\chromafile\command "" '$context_menu_command'
SectionEnd

Section /o "    Make default file browser (experimental!)" SecDefault
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "chromafile"
	WriteRegStr HKCR CompressedFolder\Shell "" "chromafile"
	WriteRegStr HKCR Drive\Shell "" "chromafile"
SectionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	WriteRegStr HKCR Directory\Shell "" "none"
	WriteRegStr HKCR CompressedFolder\Shell "" "none"
	WriteRegStr HKCR Drive\Shell "" "none"
	DeleteRegKey HKLM "${REG_UNINST_KEY}"
	DeleteRegKey HKLM Software\chromafile
	DeleteRegKey HKCR "Applications\chromafile.exe"
	DeleteRegKey HKCR Directory\shell\chromafile
	DeleteRegKey HKCR Directory\Background\shell\chromafile
	DeleteRegKey HKCR CompressedFolder\shell\chromafile
	DeleteRegKey HKCR Drive\shell\chromafile
	Delete $SMPROGRAMS\chromafile.lnk

	${un.EnumUsersReg} un.CleanupUser chromafile.temp
SectionEnd

Function un.CleanupUser
	Pop $0
	DeleteRegKey HKU "$0\Software\chromafile"
	DeleteRegValue HKU "$0\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "chromafile"
FunctionEnd

LangString DESC_SecBase ${LANG_ENGLISH} "The main application and required components."
LangString DESC_SecStart ${LANG_ENGLISH} "Add a shortcut to the start menu to launch chromafile."
LangString DESC_SecProgID ${LANG_ENGLISH} "Add an entry to the 'Open with' menu for all file types. (Does not change the default app for any file type.)"
LangString DESC_SecContext ${LANG_ENGLISH} "Add an 'Open in chromafile' command when right-clicking a folder."
LangString DESC_SecDefault ${LANG_ENGLISH} "Replace File Explorer as the default program for opening folders. Use at your own risk!"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_SecBase)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} $(DESC_SecStart)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecProgID} $(DESC_SecProgID)
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
