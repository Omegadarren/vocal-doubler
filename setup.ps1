<#
.SYNOPSIS
    Vocal Doubler - automated setup and build script.
    Installs Git, CMake, and VS C++ Build Tools if missing,
    then builds the VST3 plugin and installs it system-wide.
#>

$ErrorActionPreference = "Stop"

# Use PSScriptRoot (set when run via -File); fall back to MyInvocation for edge cases
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }

# Manual admin check with a readable message instead of #Requires (which closes silently)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
               [Security.Principal.WindowsBuiltinRole]::Administrator)
if (-not $isAdmin) {
    Write-Host ""
    Write-Host "  ERROR: This script must be run as Administrator." -ForegroundColor Red
    Write-Host "  Please right-click Setup.bat and choose 'Run as administrator'." -ForegroundColor Yellow
    Read-Host "`n  Press Enter to exit"
    exit 1
}

# ── Helpers ───────────────────────────────────────────────────────────────────

function Write-Step   ($msg) { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Write-OK     ($msg) { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Write-Warn   ($msg) { Write-Host "    [!]  $msg" -ForegroundColor Yellow }
function Write-Fail   ($msg) { Write-Host "    [ERROR] $msg" -ForegroundColor Red }

function Refresh-Path {
    $m = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    $u = [System.Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$m;$u"
}

function Find-Exe ($name, [string[]]$fallbackPaths) {
    Refresh-Path
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    foreach ($p in $fallbackPaths) {
        if (Test-Path $p) { $env:Path += ";$(Split-Path $p -Parent)"; return $p }
    }
    return $null
}

function Install-Winget-Package ($id, $displayName, [string]$override = "") {
    Write-Warn "Installing $displayName via winget..."
    $args = @(
        "install", "--id", $id,
        "--silent",
        "--accept-package-agreements",
        "--accept-source-agreements"
    )
    if ($override) { $args += @("--override", $override) }
    $proc = Start-Process -FilePath "winget" -ArgumentList $args -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -notin @(0, -1978335189)) {   # -1978335189 = already installed
        Write-Fail "winget exited with code $($proc.ExitCode) while installing $displayName"
        throw "Installation of $displayName failed."
    }
    Write-OK "$displayName installed"
}

# ── Main (wrapped so any crash prints + waits instead of closing silently) ───
try {

# ── Banner ────────────────────────────────────────────────────────────────────
Clear-Host
Write-Host ""
Write-Host "  ============================================================" -ForegroundColor Cyan
Write-Host "   VOCAL DOUBLER - Automated Setup" -ForegroundColor White
Write-Host "  ============================================================" -ForegroundColor Cyan
Write-Host "  This will:"
Write-Host "    1. Install Git, CMake, and C++ Build Tools if missing"
Write-Host "    2. Download the JUCE audio framework (~250 MB, first run only)"
Write-Host "    3. Compile and install the VST3 plugin"
Write-Host ""
Write-Host "  FIRST BUILD:   ~15-30 min (large downloads + compile)"
Write-Host "  REBUILD:       ~2-3 min"
Write-Host ""
Read-Host "  Press Enter to begin (or Ctrl+C to cancel)"

# ── 1. winget ─────────────────────────────────────────────────────────────────
Write-Step "Step 1/5 - Checking winget (Windows Package Manager)..."
$wg = Get-Command winget -ErrorAction SilentlyContinue
if (-not $wg) {
    Write-Fail "winget not found."
    Write-Host ""
    Write-Host "  winget is built into Windows 10 (v1809+) and Windows 11."
    Write-Host "  If it is missing, open the Microsoft Store and install"
    Write-Host "  'App Installer', then re-run Setup.bat."
    Read-Host "`n  Press Enter to exit"
    exit 1
}
Write-OK "winget is available"

# ── 2. Git ────────────────────────────────────────────────────────────────────
Write-Step "Step 2/5 - Checking Git..."
$gitExe = Find-Exe "git" @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files (x86)\Git\cmd\git.exe"
)
if ($gitExe) {
    Write-OK "Git already installed: $gitExe"
} else {
    Install-Winget-Package "Git.Git" "Git"
    $gitExe = Find-Exe "git" @("C:\Program Files\Git\cmd\git.exe")
    if (-not $gitExe) { throw "Git not found after install. Please restart Setup.bat." }
    Write-OK "Git ready: $gitExe"
}

# ── 3. CMake ──────────────────────────────────────────────────────────────────
Write-Step "Step 3/5 - Checking CMake..."
$cmakeExe = Find-Exe "cmake" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\CMake\bin\cmake.exe"
)
if ($cmakeExe) {
    Write-OK "CMake already installed: $cmakeExe"
} else {
    Install-Winget-Package "Kitware.CMake" "CMake"
    $cmakeExe = Find-Exe "cmake" @("C:\Program Files\CMake\bin\cmake.exe")
    if (-not $cmakeExe) { throw "CMake not found after install. Please restart Setup.bat." }
    Write-OK "CMake ready: $cmakeExe"
}

# ── 4. Visual Studio C++ tools ────────────────────────────────────────────────
Write-Step "Step 4/5 - Checking Visual Studio C++ Build Tools..."

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVCTools = $false
$vsGenerator = $null

function Get-VSGenerator ($installs) {
    # Pick the highest installed version and return the matching CMake generator string
    foreach ($inst in ($installs | Sort-Object { [version]$_.installationVersion } -Descending)) {
        $major = ([version]$inst.installationVersion).Major
        switch ($major) {
            17 { return "Visual Studio 17 2022" }
            16 { return "Visual Studio 16 2019" }
            15 { return "Visual Studio 15 2017" }
        }
    }
    return $null
}

if (Test-Path $vswhere) {
    try {
        $raw = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json 2>$null
        $installs = $raw | ConvertFrom-Json
        if ($installs -and $installs.Count -gt 0) {
            $hasVCTools = $true
            $vsGenerator = Get-VSGenerator $installs
            Write-OK "C++ tools found in: $($installs[0].displayName) ($($installs[0].installationPath))"
            Write-OK "Using CMake generator: $vsGenerator"
        }
    } catch { }
}

if (-not $hasVCTools) {
    Write-Warn "C++ Build Tools not found."
    Write-Host ""
    Write-Host "  Installing Visual Studio 2022 Build Tools with C++ workload."
    Write-Host "  This is a ~3-5 GB download and may take 15-25 minutes." -ForegroundColor Yellow
    Write-Host "  The window may appear frozen - this is normal. Please wait." -ForegroundColor Yellow
    Write-Host ""

    $vsOverride = "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    Install-Winget-Package "Microsoft.VisualStudio.2022.BuildTools" "VS 2022 Build Tools" $vsOverride

    # Re-check
    if (Test-Path $vswhere) {
        $raw = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json 2>$null
        $installs = $raw | ConvertFrom-Json
        if ($installs -and $installs.Count -gt 0) {
            $hasVCTools = $true
            $vsGenerator = Get-VSGenerator $installs
        }
    }
    if (-not $hasVCTools) {
        Write-Fail "Build Tools installed but C++ tools not detected. Please run Setup.bat again."
        Read-Host "Press Enter to exit"
        exit 1
    }
    Write-OK "C++ Build Tools ready"
}

if (-not $vsGenerator) {
    Write-Fail "Could not determine Visual Studio version. Supported: 2017, 2019, 2022."
    Read-Host "Press Enter to exit"
    exit 1
}

# ── 5. Build the plugin ───────────────────────────────────────────────────────
Write-Step "Step 5/5 - Building Vocal Doubler VST3..."
Write-Host "  (First run downloads JUCE audio framework ~250 MB - please wait)" -ForegroundColor Yellow

Set-Location $scriptDir

# Clear any stale CMake cache so generator changes don't cause errors
if (Test-Path "build\CMakeCache.txt") {
    Write-Host "  Clearing stale CMake cache..." -ForegroundColor Yellow
    Remove-Item "build" -Recurse -Force
}

# CMake configure - use the generator matched to whatever VS is installed
Write-Host "`n  [1/2] CMake configure..." -ForegroundColor White
& cmake -B build -G $vsGenerator -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Fail "CMake configure failed (exit code $LASTEXITCODE)"
    Read-Host "`nPress Enter to exit"
    exit 1
}

# CMake build
Write-Host "`n  [2/2] Compiling (Release)..." -ForegroundColor White
& cmake --build build --config Release --parallel
# Exit code 1 here is often just the post-build VST3 copy failing due to permissions.
# Check for the actual compiled artefact before reporting failure.
$artefact = "build\VocalDoubler_artefacts\Release\VST3\Vocal Doubler.vst3"
if ($LASTEXITCODE -ne 0) {
    if (-not (Test-Path $artefact)) {
        Write-Fail "Build failed (exit code $LASTEXITCODE)"
        Read-Host "`nPress Enter to exit"
        exit 1
    }
    Write-Warn "Compiled OK. Copying VST3 to Program Files..."
    Copy-Item -Recurse -Force $artefact "$env:CommonProgramFiles\VST3\"
}

# ── Done ──────────────────────────────────────────────────────────────────────
$vst3Path = "${env:CommonProgramFiles}\VST3\Vocal Doubler.vst3"
Write-Host ""
Write-Host "  ============================================================" -ForegroundColor Green
Write-Host "   SUCCESS!  Vocal Doubler is installed." -ForegroundColor Green
Write-Host "  ============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  VST3 location:" -ForegroundColor White
Write-Host "    $vst3Path" -ForegroundColor Cyan
Write-Host ""
Write-Host "  To use in Reaper:" -ForegroundColor White
Write-Host "    Options > Preferences > Plug-ins > VST > Re-scan" -ForegroundColor White
Write-Host ""
Read-Host "  Press Enter to close"

} catch {
    Write-Host ""
    Write-Host "  ============================================================" -ForegroundColor Red
    Write-Host "   SETUP FAILED" -ForegroundColor Red
    Write-Host "  ============================================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Error: $_" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "  Press Enter to close"
    exit 1
}
