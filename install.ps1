# Yolish Installer for Windows
# Run as Administrator:
#   powershell -ExecutionPolicy Bypass -File install.ps1

$ErrorActionPreference = "Stop"
$version    = "v1.0"
$repo       = "rahadbhuiya/yolish"
$installDir = "$env:ProgramFiles\Yolish"

function Write-Header {
    Write-Host ""
    Write-Host "  ██  ██  ██  ██   ██" -ForegroundColor Cyan
    Write-Host "   ██  ██  ██   ██ " -ForegroundColor Cyan
    Write-Host "    ██  ██  ██  ██  " -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Yolish $version  Installer" -ForegroundColor White
    Write-Host "  The Exploidus Language" -ForegroundColor DarkGray
    Write-Host ""
}

Write-Header

#  Step 1: Create install directory 
Write-Host "[1/6] Creating install directory..." -ForegroundColor Yellow
if (!(Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}
Write-Host "      $installDir" -ForegroundColor DarkGray

#  Step 2: Download or copy ys.exe 
Write-Host "[2/6] Installing ys.exe..." -ForegroundColor Yellow
$dest = "$installDir\ys.exe"

if (Test-Path ".\ys.exe") {
    Copy-Item ".\ys.exe" $dest -Force
    Write-Host "      Copied from local directory" -ForegroundColor DarkGray
} else {
    $url = "https://github.com/$repo/releases/download/$version/ys.exe"
    Write-Host "      Downloading from GitHub..." -ForegroundColor DarkGray
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    } catch {
        Write-Host ""
        Write-Host "  ERROR: Could not download ys.exe" -ForegroundColor Red
        Write-Host "  Download manually from:" -ForegroundColor Red
        Write-Host "  https://github.com/$repo/releases" -ForegroundColor Cyan
        exit 1
    }
}
Write-Host "      ys.exe installed" -ForegroundColor DarkGray

#  Step 3: Copy ys.exe icon (.ico)
Write-Host "[3/6] Installing exe icon..." -ForegroundColor Yellow
$exeIconSrc  = "$env:HOMEDRIVE$env:HOMEPATH\icons\ys.ico"
$exeIconPath = "$installDir\ys.ico"

if (Test-Path $exeIconSrc) {
    Copy-Item $exeIconSrc $exeIconPath -Force
    Write-Host "      ys.exe icon copied: $exeIconPath" -ForegroundColor DarkGray
} else {
    Write-Host "      Warning: ys.ico not found at $exeIconSrc (non-critical)" -ForegroundColor DarkYellow
    $exeIconPath = $null
}

#  Step 4: Copy .y file icon (.png)
Write-Host "[4/6] Installing file icon..." -ForegroundColor Yellow
$iconSrc  = "$env:HOMEDRIVE$env:HOMEPATH\icons\file.png"
$iconPath = "$installDir\file.png"

if (Test-Path $iconSrc) {
    Copy-Item $iconSrc $iconPath -Force
    Write-Host "      Icon copied: $iconPath" -ForegroundColor DarkGray
} else {
    Write-Host "      Warning: file.png not found at $iconSrc (non-critical)" -ForegroundColor DarkYellow
    $iconPath = $null
}

#  Step 5: Add to system PATH 
Write-Host "[5/6] Adding to PATH..." -ForegroundColor Yellow
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($machinePath -notlike "*$installDir*") {
    [Environment]::SetEnvironmentVariable(
        "Path", "$machinePath;$installDir", "Machine"
    )
    Write-Host "      Added: $installDir" -ForegroundColor DarkGray
} else {
    Write-Host "      Already in PATH" -ForegroundColor DarkGray
}
$env:PATH = "$env:PATH;$installDir"

#  Step 6: Register .y file association 
Write-Host "[6/6] Registering .y file type..." -ForegroundColor Yellow
try {
    $reg = "HKLM:\SOFTWARE\Classes"

    New-Item -Path "$reg\.y" -Force | Out-Null
    Set-ItemProperty -Path "$reg\.y" -Name "(Default)" -Value "YolishFile"

    New-Item -Path "$reg\YolishFile" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile" -Name "(Default)" -Value "Yolish Source File"

    # File icon (.png)
    if ($iconPath -and (Test-Path $iconPath)) {
        New-Item -Path "$reg\YolishFile\DefaultIcon" -Force | Out-Null
        Set-ItemProperty -Path "$reg\YolishFile\DefaultIcon" -Name "(Default)" -Value "`"$iconPath`",0"
        Write-Host "      Icon registered for .y files" -ForegroundColor DarkGray
    }

    # Commands
    New-Item -Path "$reg\YolishFile\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile\shell\open\command" -Name "(Default)" -Value "`"$dest`" `"%1`""

    New-Item -Path "$reg\YolishFile\shell\run" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile\shell\run" -Name "(Default)" -Value "Run with Yolish"
    New-Item -Path "$reg\YolishFile\shell\run\command" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile\shell\run\command" -Name "(Default)" -Value "`"$dest`" `"%1`""

    New-Item -Path "$reg\YolishFile\shell\compile" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile\shell\compile" -Name "(Default)" -Value "Compile with Yolish"
    New-Item -Path "$reg\YolishFile\shell\compile\command" -Force | Out-Null
    Set-ItemProperty -Path "$reg\YolishFile\shell\compile\command" -Name "(Default)" -Value "`"$dest`" -c `"%1`""

    # Refresh icons
    $source = @"
using System;
using System.Runtime.InteropServices;
public class Shell32 {
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(int wEventId, int uFlags, IntPtr dwItem1, IntPtr dwItem2);
}
"@
    Add-Type -TypeDefinition $source
    [Shell32]::SHChangeNotify(0x08000000, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero)

    Write-Host "      .y files registered" -ForegroundColor DarkGray
} catch {
    Write-Host "      Warning: Could not register file type (non-critical)" -ForegroundColor DarkYellow
}

# Done
Write-Host ""
Write-Host "  Yolish installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "  Open a new terminal and try:" -ForegroundColor White
Write-Host "    ys                   start REPL" -ForegroundColor Cyan
Write-Host "    ys hello.y           run a file" -ForegroundColor Cyan
Write-Host "    ys -c hello.y        compile to native binary" -ForegroundColor Cyan
Write-Host ""
Write-Host "  VS Code: install the Yolish extension for syntax highlighting." -ForegroundColor DarkGray
Write-Host ""

$open = Read-Host "  Open a new terminal now? (y/n)"
if ($open -eq "y" -or $open -eq "Y") {
    Start-Process "cmd.exe"
}
