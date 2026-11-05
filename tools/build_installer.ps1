# Snex9x v2 - Automated Installer Builder
# Locates Inno Setup, ensures Release binaries are compiled, and generates dist/Snex9x_v2_Setup_v2.0.0.exe

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
Set-Location $projectRoot

Write-Host "=== Snex9x v2 Windows Setup Installer Builder ===" -ForegroundColor Cyan

# 1. Locate ISCC.exe
$isccCandidates = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles (x86)\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)

$isccPath = $null
foreach ($candidate in $isccCandidates) {
    if ($candidate -and (Test-Path $candidate)) {
        $isccPath = $candidate
        break
    }
}

if (-not $isccPath) {
    Write-Error "Inno Setup Compiler (ISCC.exe) not found! Please install Inno Setup 6 via: winget install JRSoftware.InnoSetup"
    exit 1
}

Write-Host "[+] Found Inno Setup Compiler at: $isccPath" -ForegroundColor Green

# 2. Check Release Binaries
$releaseExe = Join-Path $projectRoot "build\Release\Snex9x_v2.exe"
$sdlDll = Join-Path $projectRoot "build\Release\SDL3.dll"

if (-not (Test-Path $releaseExe) -or -not (Test-Path $sdlDll)) {
    Write-Host "[*] Compiling Release binaries with CMake..." -ForegroundColor Yellow
    cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Release build failed!"
        exit 1
    }
}

# 3. Compile Installer
$issFile = Join-Path $projectRoot "installer\Snex9x_v2.iss"
Write-Host "[*] Compiling installer script: $issFile..." -ForegroundColor Yellow

& $isccPath $issFile

if ($LASTEXITCODE -eq 0) {
    $distDir = Join-Path $projectRoot "dist"
    $installerExe = Get-ChildItem "$distDir\Snex9x_v2_Setup_*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Write-Host "`n[SUCCESS] Installer built successfully!" -ForegroundColor Green
    if ($installerExe) {
        $sizeMb = [math]::Round($installerExe.Length / 1MB, 2)
        Write-Host "Installer package: $($installerExe.FullName) ($sizeMb MB)" -ForegroundColor Cyan
    }
} else {
    Write-Error "ISCC failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
