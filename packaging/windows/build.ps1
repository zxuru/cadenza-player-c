#Requires -Version 5.1
<#
# Builds Cadenza for Windows and gathers everything it needs to run -- the Qt
# runtime, libmpv, TagLib, the rest of the MinGW runtime and the helper tools
# -- into `dist\bin`, then turns that folder into the one file a user
# downloads and double-clicks:
#
#     dist\cadenza-<version>-setup.exe             the installer (default)
#     dist\cadenza-<version>-win64-portable.zip    -Portable, unzip and run
#     dist\bin\                                    the application itself
#
# The toolchain is MSYS2's UCRT64 environment, which packages Qt6, libmpv and
# TagLib for MinGW: https://www.msys2.org/. Nothing else has to be installed.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1
#   powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1 -Portable -SkipFfmpeg
#
#   -Msys2          MSYS2 root (default C:\msys64)
#   -BuildDir        -DistDir   where the build and the package land
#                   (default <repo>\build-windows and <repo>\dist)
#   -SkipTools      do not stage tools\ -- no yt-dlp, no music search
#   -SkipFfmpeg     stage the tools without ffmpeg: downloads then land in the
#                   container YouTube served, untagged, and the installer is
#                   ~160 MB smaller
#   -SkipInstaller  stop after dist\bin, without building the setup .exe
#   -Portable       also write the zip
#>
[CmdletBinding()]
param(
    [string]$Msys2 = 'C:\msys64',
    [string]$BuildDir,
    [string]$DistDir,
    [switch]$SkipTools,
    [switch]$SkipFfmpeg,
    [switch]$SkipInstaller,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $Repo 'build-windows' }
if (-not $DistDir) { $DistDir = Join-Path $Repo 'dist' }

$Ucrt64 = Join-Path $Msys2 'ucrt64'
$Bin = Join-Path $Ucrt64 'bin'
# qmlimportscanner, qmlcachegen and qmltyperegistrar live outside bin\; CMake
# finds them through the Qt6 package, but the tools that call each other by
# name do not.
$QtBin = Join-Path $Ucrt64 'share\qt6\bin'

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

# Prepending the UCRT64 bin directory is what makes the rest of this script
# work from an ordinary PowerShell: cmake, ninja, the compiler and Qt's own
# tools are found there and nowhere else.
$env:PATH = "$Bin;$QtBin;$env:PATH"

function Assert-Tool([string]$Name)
{
    $path = Join-Path $Bin $Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw @"
error: $path is missing.

Install the toolchain MSYS2 packages the README lists:

  pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
            mingw-w64-ucrt-x86_64-qt6-svg \
            mingw-w64-ucrt-x86_64-mpv mingw-w64-ucrt-x86_64-taglib \
            mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-ntldd \
            mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
            mingw-w64-ucrt-x86_64-gcc
"@
    }
    return $path
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments)
{
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "error: $([IO.Path]::GetFileName($Exe)) exited with $LASTEXITCODE"
    }
}

$cmake = Assert-Tool 'cmake.exe'
$windeployqt = Assert-Tool 'windeployqt.exe'
$ntldd = Assert-Tool 'ntldd.exe'
[void](Assert-Tool 'ninja.exe')
[void](Assert-Tool 'g++.exe')

# MSYS2's .pc files are written for the MSYS2 root (`prefix=/ucrt64`) and a
# native pkgconf has to be told to re-derive the prefix from where the file
# was found; without it CMake is handed /ucrt64/include, which is not a path
# a Windows compiler can open. pkgconf ignores the flag when the prefix is
# already absolute, so it is always right to pass it.
$pkgconf = Join-Path $Bin 'pkgconf.exe'
$pkgArgs = @()
if (Test-Path -LiteralPath $pkgconf -PathType Leaf) {
    $pkgArgs = @("-DPKG_CONFIG_EXECUTABLE=$pkgconf", '-DPKG_CONFIG_ARGN=--define-prefix')
} else {
    [void](Assert-Tool 'pkg-config.exe')
}

function Get-ProjectVersion
{
    $source = Get-Content -LiteralPath (Join-Path $Repo 'CMakeLists.txt') -Raw
    if ($source -notmatch 'project\(\s*cadenza\s+VERSION\s+([0-9][0-9.]*)') {
        throw 'error: no version in CMakeLists.txt'
    }
    return $Matches[1]
}

# ---------------------------------------------------------------------------
# 1. The helper tools the app runs
# ---------------------------------------------------------------------------

$tools = Join-Path $Repo 'packaging\tools'
if ($SkipTools) {
    Write-Host '==> skipping the helper tools (-SkipTools)'
} else {
    $fetch = Join-Path $tools 'fetch-tools.ps1'
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $fetch)
    if (-not $SkipFfmpeg) { $arguments += '-Ffmpeg' }
    Invoke-Checked 'powershell.exe' $arguments
}

# What is staged is what CMake installs, whichever of the two scripts put it
# there: `-SkipFfmpeg` stages nothing but does not take away an ffmpeg.exe an
# earlier build left behind, and that is worth saying out loud.
foreach ($name in 'yt-dlp.exe', 'qjs.exe', 'ffmpeg.exe') {
    $path = Join-Path $tools $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    Write-Host ("    {0} ({1:N1} MB)" -f $name, ((Get-Item -LiteralPath $path).Length / 1MB))
    if ($SkipFfmpeg -and $name -eq 'ffmpeg.exe') {
        Write-Warning "$path is already staged and will be packaged anyway; delete it to leave ffmpeg out"
    }
}

# ---------------------------------------------------------------------------
# 2. Build and install
# ---------------------------------------------------------------------------

Write-Host '==> configuring'
Invoke-Checked $cmake (@(
        '-S', $Repo,
        '-B', $BuildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$Ucrt64"
    ) + $pkgArgs)

Write-Host '==> building'
Invoke-Checked $cmake @('--build', $BuildDir)

Write-Host "==> installing into $DistDir"
# The install tree is regenerated every time: a library left over from an
# earlier build is the kind of thing that ships.
if (Test-Path -LiteralPath $DistDir) { Remove-Item -LiteralPath $DistDir -Recurse -Force }
Invoke-Checked $cmake @('--install', $BuildDir, '--prefix', $DistDir)

$exe = Join-Path $DistDir 'bin\cadenza.exe'

# ---------------------------------------------------------------------------
# 3. The runtime beside it
# ---------------------------------------------------------------------------

Write-Host '==> Qt runtime'
Invoke-Checked $windeployqt @(
    '--release',
    # The app carries its own translations, so Qt's are ~15 MB of nothing.
    '--no-translations',
    # windeployqt deploys a plugin when the module it belongs to is in use,
    # which is what decides these anyway -- but two of them are the difference
    # between working and looking like it works: without `imageformats` there
    # is no JPEG decoder and every cover is blank, and without `tls` there is
    # no HTTPS and the lyrics lookup fails. Asking for them costs a few
    # hundred kilobytes and takes the guesswork out.
    '--add-plugin-types', 'imageformats,tls',
    '--qmldir', (Join-Path $Repo 'src\ui'),
    $exe
)

# windeployqt knows Qt and nothing else: libmpv, TagLib and the MinGW runtime
# (libgcc, libstdc++, libwinpthread) come from MSYS2, and libmpv-2.dll drags
# a whole FFmpeg, ass, fontconfig and harfbuzz tree behind it. ntldd walks
# that closure, which is the only dependable way to learn what is missing --
# a missing DLL is a silent failure to start, not an error message.
Write-Host '==> library closure'
$copied = New-Object System.Collections.Generic.List[string]
for ($pass = 1; $pass -le 3; $pass++) {
    $lines = & $ntldd -R $exe
    $added = 0
    foreach ($line in $lines) {
        if ($line -notmatch '=>\s*([A-Za-z]:\\[^()]+?)\s*\(0x') { continue }
        $resolved = $Matches[1].Trim()
        if (-not $resolved.StartsWith($Bin, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $name = [IO.Path]::GetFileName($resolved)
        $target = Join-Path $DistDir "bin\$name"
        if (Test-Path -LiteralPath $target) { continue }
        Copy-Item -LiteralPath $resolved -Destination $target
        $copied.Add($name)
        $added++
    }
    if ($added -eq 0) { break }
}
$copied | Sort-Object -Unique | ForEach-Object { Write-Host "    + $_" }

$unresolved = @(& $ntldd -R $exe | Select-String -Pattern 'not found' -SimpleMatch |
    ForEach-Object { ($_.Line -replace '\s*=>.*', '').Trim() } |
    Where-Object { $_ -and $_ -notmatch '^(api-ms-|ext-ms-)' })
if ($unresolved.Count -gt 0) {
    Write-Warning "ntldd could not resolve: $($unresolved -join ', ')"
}

# What the closure is for, checked: a package that leaves libmpv or TagLib
# behind is one that fails on the user's machine with Windows' own "the code
# execution cannot proceed", which tells them nothing and us less.
foreach ($pattern in 'libmpv*.dll', 'libtag*.dll') {
    $found = Get-ChildItem -LiteralPath (Join-Path $DistDir 'bin') -Filter $pattern -File
    if (-not $found) {
        throw "error: no $pattern beside the executable; the package would not start"
    }
    Write-Host "    $($found[0].Name)"
}

# ---------------------------------------------------------------------------
# 4. The one file to download
# ---------------------------------------------------------------------------

$version = Get-ProjectVersion
$artifacts = New-Object System.Collections.Generic.List[string]

if (-not $SkipInstaller) {
    $iscc = @()
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) { $iscc += Join-Path $programFilesX86 'Inno Setup 6\ISCC.exe' }
    if ($env:ProgramFiles) { $iscc += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe' }
    $iscc = $iscc | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $iscc) {
        $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
        if ($command) { $iscc = $command.Source }
    }

    if (-not $iscc) {
        Write-Warning 'Inno Setup (ISCC.exe) is not installed; skipping the installer. https://jrsoftware.org/isdl.php'
    } else {
        Write-Host '==> installer'
        Invoke-Checked $iscc @(
            "/DAppVersion=$version",
            "/DSourceDir=$DistDir\bin",
            "/DOutputDir=$DistDir",
            (Join-Path $PSScriptRoot 'cadenza.iss')
        )
        $artifacts.Add((Join-Path $DistDir "cadenza-$version-setup.exe"))
    }
}

if ($Portable) {
    Write-Host '==> portable zip'
    $zip = Join-Path $DistDir "cadenza-$version-win64-portable.zip"
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $DistDir 'bin\*') -DestinationPath $zip
    $artifacts.Add($zip)
}

Write-Host '==> done'
Write-Host ("    {0} ({1:N1} MB)" -f $DistDir, ((Get-ChildItem -LiteralPath $DistDir -Recurse -File |
                Measure-Object -Property Length -Sum).Sum / 1MB))
foreach ($artifact in $artifacts) {
    Write-Host ("    {0} ({1:N1} MB)" -f $artifact, ((Get-Item -LiteralPath $artifact).Length / 1MB))
}
