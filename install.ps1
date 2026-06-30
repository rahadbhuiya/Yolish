# Yolish Installer for Windows
# Run from the folder where install.ps1 is located:
#   powershell -ExecutionPolicy Bypass -File .\install.ps1
#
# Admin is optional — without it, installs to %LOCALAPPDATA%\Yolish instead of Program Files.

$ErrorActionPreference = "Stop"
$version = "v2.6"
$repo    = "rahadbhuiya/yolish"

# Use Program Files if Admin, else fallback to user-local dir (no Admin needed)
$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

function Write-Header {
    Write-Host ""
    Write-Host "  ==============================" -ForegroundColor Cyan
    Write-Host "    Yolish $version Installer" -ForegroundColor White
    Write-Host "    The Exploidus Language" -ForegroundColor DarkGray
    Write-Host "  ==============================" -ForegroundColor Cyan
    Write-Host ""
}

Write-Header

if ($isAdmin) {
    $installDir = "$env:ProgramFiles\Yolish"
} else {
    $installDir = "$env:LOCALAPPDATA\Yolish"
    Write-Host "  Note: Not running as Administrator." -ForegroundColor DarkYellow
    Write-Host "  Installing to: $installDir" -ForegroundColor DarkYellow
    Write-Host "  For a system-wide install, re-run as Administrator." -ForegroundColor DarkYellow
    Write-Host ""
}

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

#  Step 3: Get ys.ico (local or download)
Write-Host "[3/6] Installing exe icon..." -ForegroundColor Yellow
$exeIconSrc  = "$PSScriptRoot\icons\ys.ico"
$exeIconPath = "$installDir\ys.ico"
New-Item -ItemType Directory -Path "$installDir\icons" -Force | Out-Null

if (Test-Path $exeIconSrc) {
    Copy-Item $exeIconSrc $exeIconPath -Force
    Write-Host "      ys.ico copied from local" -ForegroundColor DarkGray
} else {
    Write-Host "      Downloading ys.ico from GitHub..." -ForegroundColor DarkGray
    try {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/$repo/master/icons/ys.ico" `
            -OutFile $exeIconPath -UseBasicParsing
        Write-Host "      ys.ico downloaded" -ForegroundColor DarkGray
    } catch {
        Write-Host "      Warning: Could not get ys.ico (non-critical)" -ForegroundColor DarkYellow
        $exeIconPath = $null
    }
}

#  Step 4: Copy .y file icon (convert PNG -> ICO for Windows)
Write-Host "[4/6] Installing file icon..." -ForegroundColor Yellow
$iconSrc  = "$PSScriptRoot\icons\file.png"
$iconPath = "$installDir\file.ico"

if (!(Test-Path $iconSrc)) {
    Write-Host "      Downloading file.png from GitHub..." -ForegroundColor DarkGray
    try {
        New-Item -ItemType Directory -Path "$PSScriptRoot\icons" -Force | Out-Null
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/$repo/master/icons/file.png" `
            -OutFile $iconSrc -UseBasicParsing
        Write-Host "      file.png downloaded" -ForegroundColor DarkGray
    } catch {
        Write-Host "      Warning: Could not download file.png (non-critical)" -ForegroundColor DarkYellow
    }
}

if (Test-Path $iconSrc) {
    # Convert PNG to ICO using .NET System.Drawing
    try {
        Add-Type -AssemblyName System.Drawing
        $png    = [System.Drawing.Image]::FromFile($iconSrc)
        $bitmap = New-Object System.Drawing.Bitmap(256, 256)
        $g      = [System.Drawing.Graphics]::FromImage($bitmap)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($png, 0, 0, 256, 256)
        $g.Dispose()
        $png.Dispose()

        # Write ICO file (header + 256x256 PNG chunk)
        $ms = New-Object System.IO.MemoryStream
        $bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        $pngBytes = $ms.ToArray()
        $ms.Dispose()

        $fs = [System.IO.File]::OpenWrite($iconPath)
        $bw = New-Object System.IO.BinaryWriter($fs)
        # ICO header
        $bw.Write([uint16]0)      # reserved
        $bw.Write([uint16]1)      # type: icon
        $bw.Write([uint16]1)      # image count
        # Directory entry (256x256 PNG)
        $bw.Write([byte]0)        # width  (0 = 256)
        $bw.Write([byte]0)        # height (0 = 256)
        $bw.Write([byte]0)        # color count
        $bw.Write([byte]0)        # reserved
        $bw.Write([uint16]1)      # planes
        $bw.Write([uint16]32)     # bit count
        $bw.Write([uint32]$pngBytes.Length)
        $bw.Write([uint32]22)     # offset (6 header + 16 dir entry)
        $bw.Write($pngBytes)
        $bw.Dispose()
        $fs.Dispose()

        Write-Host "      file.ico created: $iconPath" -ForegroundColor DarkGray
    } catch {
        Write-Host "      Warning: Could not convert icon (non-critical)" -ForegroundColor DarkYellow
        $iconPath = $null
    }
} else {
    Write-Host "      Warning: file.png not found at $iconSrc (non-critical)" -ForegroundColor DarkYellow
    $iconPath = $null
}

#  Step 5: Add to system PATH 
Write-Host "[5/6] Adding to PATH..." -ForegroundColor Yellow
if ($isAdmin) {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($machinePath -notlike "*$installDir*") {
        [Environment]::SetEnvironmentVariable("Path", "$machinePath;$installDir", "Machine")
        Write-Host "      Added to system PATH: $installDir" -ForegroundColor DarkGray
    } else {
        Write-Host "      Already in system PATH" -ForegroundColor DarkGray
    }
} else {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath -notlike "*$installDir*") {
        [Environment]::SetEnvironmentVariable("Path", "$userPath;$installDir", "User")
        Write-Host "      Added to user PATH: $installDir" -ForegroundColor DarkGray
    } else {
        Write-Host "      Already in PATH" -ForegroundColor DarkGray
    }
}
$env:PATH = "$env:PATH;$installDir"

#  Step 6: Register .y file association 
Write-Host "[6/6] Registering .y file type..." -ForegroundColor Yellow
try {
    # HKCU works without Admin; HKLM needs Admin (system-wide)
    if ($isAdmin) {
        $reg = "HKLM:\SOFTWARE\Classes"
    } else {
        $reg = "HKCU:\SOFTWARE\Classes"
    }

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