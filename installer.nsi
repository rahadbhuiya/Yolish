; ================================================================
;  Yolish Programming Language — Windows GUI Installer
;  NSIS Modern UI 2.0
;  Builds on Linux via: makensis installer.nsi
;  Requires: ys.exe and icons/ys.ico in the same directory
; ================================================================

!include "MUI2.nsh"
!include "x64.nsh"
!include "WinMessages.nsh"

;  Basic info 
Name              "Yolish Programming Language"
OutFile           "yolish-setup.exe"
Unicode           True
InstallDir        "$PROGRAMFILES64\Yolish"
InstallDirRegKey  HKLM "Software\Yolish" "InstallDir"
RequestExecutionLevel admin
SetCompressor     /SOLID lzma

;  Installer metadata 
VIProductVersion  "2.6.0.0"
VIAddVersionKey   "ProductName"      "Yolish Programming Language"
VIAddVersionKey   "ProductVersion"   "v2.6"
VIAddVersionKey   "CompanyName"      ".Bhuiya"
VIAddVersionKey   "FileDescription"  "Yolish Installer"
VIAddVersionKey   "FileVersion"      "2.6.0.0"
VIAddVersionKey   "LegalCopyright"   "MIT License"

;  MUI settings 
!define MUI_ABORTWARNING
!define MUI_ICON                        "icons\ys.ico"
!define MUI_UNICON                      "icons\ys.ico"
!define MUI_WELCOMEPAGE_TITLE           "Welcome to Yolish v2.6"
!define MUI_WELCOMEPAGE_TEXT            "This wizard will install the Yolish programming language on your computer.$\r$\n$\r$\nYolish is the official language of Exploidus OS.$\r$\nFast, expressive, capability-aware.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE            "Yolish v2.6 installed"
!define MUI_FINISHPAGE_TEXT             "Yolish has been installed.$\r$\n$\r$\nOpen a new terminal and type:$\r$\n$\r$\n  ys          (REPL)$\r$\n  ys hello.y  (run a file)$\r$\n$\r$\nOr launch the Yolish REPL from the Start Menu."
!define MUI_FINISHPAGE_RUN              "$INSTDIR\yolish-repl.bat"
!define MUI_FINISHPAGE_RUN_TEXT         "Launch Yolish REPL now"
!define MUI_FINISHPAGE_SHOWREADME       ""
!define MUI_FINISHPAGE_LINK             "github.com/rahadbhuiya/Yolish"
!define MUI_FINISHPAGE_LINK_LOCATION    "https://github.com/rahadbhuiya/Yolish"

;  Installer pages (in order) 
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

;  Uninstaller pages 
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;  Language (must come after all MUI macros) 
!insertmacro MUI_LANGUAGE "English"

; ================================================================
;  Install section
; ================================================================
Section "Yolish" SecMain

  SetOutPath "$INSTDIR"

  ;  Copy the Yolish binary and icon 
  File "ys.exe"
  File "icons\ys.ico"

  ; --- Write a small batch file used by the Start Menu shortcut -
  ; The shortcut points at this .bat so the window title and loop
  ; logic are controlled here rather than inside the .lnk itself.
  FileOpen  $0 "$INSTDIR\yolish-repl.bat" w
  FileWrite $0 "@echo off$\r$\n"
  FileWrite $0 "title Yolish v2.6 - REPL$\r$\n"
  FileWrite $0 "cd /d %USERPROFILE%$\r$\n"
  FileWrite $0 "echo Yolish v2.6  --  type exit to quit$\r$\n"
  FileWrite $0 "echo.$\r$\n"
  FileWrite $0 '"$INSTDIR\ys.exe"$\r$\n'
  FileClose $0

  ;  Add $INSTDIR to the system PATH (Machine scope) 
  ; Read the current PATH, append our dir if not already present.
  ReadRegStr $1 HKLM \
    "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
    "Path"
  ; Check whether $INSTDIR is already in PATH to avoid duplicates.
  Push "$1"
  Push "$INSTDIR"
  Call StrContains
  Pop $2
  ${If} $2 == ""
    ; Not found — append it.
    ${If} $1 != ""
      StrCpy $1 "$1;$INSTDIR"
    ${Else}
      StrCpy $1 "$INSTDIR"
    ${EndIf}
    WriteRegExpandStr HKLM \
      "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
      "Path" "$1"
    ; Broadcast the environment change so open Explorer windows,
    ; new cmd windows, etc. pick it up without a reboot.
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 \
      "STR:Environment" /TIMEOUT=5000
  ${EndIf}

  ;  Start Menu 
  CreateDirectory "$SMPROGRAMS\Yolish"
  CreateShortcut \
    "$SMPROGRAMS\Yolish\Yolish REPL.lnk" \
    "$INSTDIR\yolish-repl.bat" \
    "" \
    "$INSTDIR\ys.ico" 0 \
    SW_SHOWNORMAL \
    "" \
    "Open the Yolish interactive REPL"
  CreateShortcut \
    "$SMPROGRAMS\Yolish\Uninstall Yolish.lnk" \
    "$INSTDIR\Uninstall.exe"

  ;  Add/Remove Programs registry entry 
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "DisplayName"     "Yolish Programming Language"
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "DisplayVersion"  "v2.6"
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "Publisher"       ".Bhuiya"
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "DisplayIcon"     "$INSTDIR\ys.ico"
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "URLInfoAbout"    "https://github.com/rahadbhuiya/Yolish"
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "NoModify" 1
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish" \
    "NoRepair"  1

  ; Save install dir for the uninstaller
  WriteRegStr HKLM "Software\Yolish" "InstallDir" "$INSTDIR"

  ; Write the uninstaller itself
  WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

; ================================================================
;  Uninstall section
; ================================================================
Section "Uninstall"

  ;  Remove files 
  Delete "$INSTDIR\ys.exe"
  Delete "$INSTDIR\ys.ico"
  Delete "$INSTDIR\yolish-repl.bat"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir  "$INSTDIR"

  ;  Remove Start Menu shortcuts 
  Delete "$SMPROGRAMS\Yolish\Yolish REPL.lnk"
  Delete "$SMPROGRAMS\Yolish\Uninstall Yolish.lnk"
  RMDir  "$SMPROGRAMS\Yolish"

  ;  Remove $INSTDIR from the system PATH 
  ReadRegStr $1 HKLM \
    "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
    "Path"
  ; Strip ";INSTDIR" or "INSTDIR;" from the middle/end/start.
  Push "$1"
  Push ";$INSTDIR"
  Call un.StrReplace
  Pop $1
  Push "$1"
  Push "$INSTDIR;"
  Call un.StrReplace
  Pop $1
  ; Edge case: was the only entry (no semicolons at all)
  Push "$1"
  Push "$INSTDIR"
  Call un.StrReplace
  Pop $1
  WriteRegExpandStr HKLM \
    "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
    "Path" "$1"
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 \
    "STR:Environment" /TIMEOUT=5000

  ;  Registry cleanup 
  DeleteRegKey HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Yolish"
  DeleteRegKey HKLM "Software\Yolish"

SectionEnd

; ================================================================
;  Helper: StrContains
;  Checks whether $R1 contains $R0.
;  Usage: Push "haystack"  Push "needle"  Call StrContains  Pop $result
;  Result is the needle string if found, empty string if not found.
; ================================================================
Function StrContains
  Exch $R0   ; needle
  Exch
  Exch $R1   ; haystack
  Push $R2
  Push $R3
  Push $R4
  StrLen $R3 "$R0"
  StrCpy $R4 0
  loop:
    StrCpy $R2 "$R1" $R3 $R4
    StrCmp $R2 "" not_found
    StrCmp $R2 "$R0" found
    IntOp $R4 $R4 + 1
    Goto loop
  found:
    StrCpy $R0 "$R0"
    Goto done
  not_found:
    StrCpy $R0 ""
  done:
  Pop $R4
  Pop $R3
  Pop $R2
  Exch $R1
  Exch
  Exch $R0
FunctionEnd

; ================================================================
;  Helper: StrReplace (installer context)
;  Usage: Push "source"  Push "search"  Call StrReplace  Pop $result
; ================================================================
Function StrReplace
  Exch $R0   ; search
  Exch
  Exch $R1   ; source
  Push $R2
  Push $R3
  Push $R4
  Push $R5
  StrCpy $R2 ""
  StrLen $R3 "$R0"
  StrCpy $R4 0
  loop:
    StrCpy $R5 "$R1" $R3 $R4
    StrCmp $R5 "" done
    StrCmp $R5 "$R0" found
    StrCpy $R5 "$R1" 1 $R4
    StrCpy $R2 "$R2$R5"
    IntOp $R4 $R4 + 1
    Goto loop
  found:
    IntOp $R4 $R4 + $R3
    Goto loop
  done:
    ; Append any remaining characters after the last match
    StrCpy $R5 "$R1" "" $R4
    StrCpy $R2 "$R2$R5"
  StrCpy $R0 "$R2"
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Exch $R1
  Exch
  Exch $R0
FunctionEnd

; ================================================================
;  Helper: StrReplace (uninstaller context — must be re-declared)
; ================================================================
Function un.StrReplace
  Exch $R0
  Exch
  Exch $R1
  Push $R2
  Push $R3
  Push $R4
  Push $R5
  StrCpy $R2 ""
  StrLen $R3 "$R0"
  StrCpy $R4 0
  un.loop:
    StrCpy $R5 "$R1" $R3 $R4
    StrCmp $R5 "" un.done
    StrCmp $R5 "$R0" un.found
    StrCpy $R5 "$R1" 1 $R4
    StrCpy $R2 "$R2$R5"
    IntOp $R4 $R4 + 1
    Goto un.loop
  un.found:
    IntOp $R4 $R4 + $R3
    Goto un.loop
  un.done:
    StrCpy $R5 "$R1" "" $R4
    StrCpy $R2 "$R2$R5"
  StrCpy $R0 "$R2"
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Exch $R1
  Exch
  Exch $R0
FunctionEnd
