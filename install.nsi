!include LogicLib.nsh
!include MUI2.nsh
!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\chromabrowse"

Name "chromabrowse"
OutFile "build\chromabrowse-setup.exe"
RequestExecutionLevel admin
Unicode True

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
	File build\chromabrowse.exe
SectionEnd

Section "Start Menu Shortcut"
	CreateShortcut /NoWorkingDir "$SMPROGRAMS\chromabrowse.lnk" "$INSTDIR\chromabrowse.exe"
SectionEnd

Section "un.Uninstall"
	Delete $INSTDIR\*.exe
	RMDir $INSTDIR
	SetRegView 64
	DeleteRegKey HKLM "${REG_UNINST_KEY}"
	DeleteRegKey HKLM Software\chromabrowse
	Delete $SMPROGRAMS\chromabrowse.lnk
SectionEnd
