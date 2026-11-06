Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
Name "即時網路速度 ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\RealtimeNetworkSpeed"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1028 "ProductName" "即時網路速度"
VIAddVersionKey /LANG=1028 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1028 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1028 "FileDescription" "即時網路速度安裝程式"
VIAddVersionKey /LANG=1028 "LegalCopyright" "Copyright (c) 2026 Kaido-MSVC"
!define KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\RealtimeNetworkSpeed"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "TradChinese"
Section "Install"
    SetShellVarContext current
    SetRegView 64
    SetOverwrite on
    ClearErrors
    !insertmacro InstallPayload
    IfErrors 0 +3
        MessageBox MB_ICONSTOP "無法寫入程式檔案，請退出即時網路速度後重新安裝。"
        Abort
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\RealtimeNetworkSpeed"
    CreateShortcut "$SMPROGRAMS\RealtimeNetworkSpeed\即時網路速度.lnk" "$INSTDIR\RealtimeNetworkSpeed.exe"
    WriteRegStr HKCU "${KEY}" "DisplayName" "即時網路速度"
    WriteRegStr HKCU "${KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${KEY}" "Publisher" "Kaido-MSVC"
    WriteRegStr HKCU "${KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "${KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegStr HKCU "${KEY}" "DisplayIcon" "$INSTDIR\RealtimeNetworkSpeed.exe"
    WriteRegDWORD HKCU "${KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${KEY}" "NoRepair" 1
SectionEnd
Section "Uninstall"
    SetShellVarContext current
    SetRegView 64
    ClearErrors
    !insertmacro RemovePayload
    IfErrors 0 +3
        MessageBox MB_ICONSTOP "部分程式檔案仍被使用，請退出即時網路速度後重新移除。"
        Abort
    ReadRegStr $0 HKCU "${KEY}" "InstallLocation"
    ${If} $0 == $INSTDIR
        DeleteRegKey HKCU "${KEY}"
        Delete "$SMPROGRAMS\RealtimeNetworkSpeed\即時網路速度.lnk"
        RMDir "$SMPROGRAMS\RealtimeNetworkSpeed"
    ${EndIf}
    Delete "$INSTDIR\Uninstall.exe"
    !insertmacro RemoveEmptyPayloadDirs
    SetOutPath "$TEMP"
    RMDir "$INSTDIR"
    ; No wildcard or recursive removal; all settings/history and extra files remain.
SectionEnd
