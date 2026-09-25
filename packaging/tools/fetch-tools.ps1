#Requires -Version 5.1
<#
# Downloads the helper binaries Cadenza shells out to at runtime -- the Windows
# x64 builds of exactly the three packaging/tools/fetch-tools.sh stages for
# Linux, and for the same reason: the app looks for them in `tools\` beside
# `cadenza.exe` before it looks on PATH, so a user should not have to install
# yt-dlp, QuickJS or ffmpeg themselves.
#
# Every download is verified against a pinned SHA-256 *before* it lands in its
# final place: we fetch to "<name>.part", compare the hash, and only then
# rename. A tampered or truncated download never becomes a runnable tool.
#
# The versions below are pinned on purpose. yt-dlp in particular changes
# often, and builds older than the one below answer audio requests with HTTP
# 403 (measured), so we refuse to inherit whatever the machine has.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File packaging\tools\fetch-tools.ps1
#   powershell -ExecutionPolicy Bypass -File packaging\tools\fetch-tools.ps1 -Ffmpeg -Dest C:\cadenza\tools
#
#   -Force   re-download even when a correct file is already present
#   -Ffmpeg  also fetch ffmpeg (static build, ~180 MB archive, off by default);
#            without it the audio is saved in the container YouTube served
#   -Dest    destination directory (default: this script's own directory)
#>
[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$Ffmpeg,
    [string]$Dest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Windows PowerShell 5.1 offers TLS 1.0 first on older builds and GitHub
# refuses it; ask for 1.2 before the first request goes out.
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
}

# A progress bar per 64 KB makes a 180 MB download crawl. Quiet the status
# line; each download prints one line of its own below instead.
$ProgressPreference = 'SilentlyContinue'

# yt-dlp: the yt-dlp.exe asset. It is a PyInstaller bundle, so it carries its
# own interpreter and needs no Python on the machine. Its checksum comes from
# the release's SHA2-256SUMS file, which also lists every other asset; we look
# up only the line we care about.
$YtdlpVersion = '2026.08.19'
$YtdlpAsset = 'yt-dlp.exe'
$YtdlpBase = "https://github.com/yt-dlp/yt-dlp/releases/download/$YtdlpVersion"

# qjs: quickjs-ng ships prebuilt, statically linked binaries for each platform,
# so the windows x86_64 asset is the engine itself -- there is no archive to
# unpack. The upstream release publishes no checksum file, so this value was
# computed once from the pinned URL below and is recorded here.
#   https://github.com/quickjs-ng/quickjs/releases/download/v0.17.0/qjs-windows-x86_64.exe
$QjsVersion = 'v0.17.0'
$QjsAsset = 'qjs-windows-x86_64.exe'
$QjsSha256 = '2aeabf0092c3262d6b2609824418f7dd7ed1f1df939f73b2b15645230cac0d77'

# ffmpeg: BtbN's static win64 build, taken from a dated autobuild release --
# those tags never move, unlike the rolling `latest` one -- with the checksum
# out of the release's own checksums.sha256. Only ffmpeg.exe is kept: ffplay
# and ffprobe are another two hundred megabytes of archive for nothing this
# app runs.
$FfmpegTag = 'autobuild-2026-09-23-14-55'
$FfmpegAsset = 'ffmpeg-n8.1.3-win64-gpl-8.1.zip'
$FfmpegBase = "https://github.com/BtbN/FFmpeg-Builds/releases/download/$FfmpegTag"
# The archive as published, and the one executable we take out of it. The
# second lets a repeat run skip the download the way the other two tools do.
$FfmpegArchiveSha256 = 'fde13c8cf7c1b0a54bf661105652970810d94d140d867dd990fce4885a9f5d20'
$FfmpegExeSha256 = 'b826bced7badfa264116ba0640251410bcd0671c49d72bf66130673c11bb0ccc'

$Dest = if ($Dest) { $Dest } else { $PSScriptRoot }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$Dest = (Get-Item -LiteralPath $Dest).FullName

# staged accumulates "<name> <sha256> <bytes>" lines for the closing summary.
$staged = New-Object System.Collections.Generic.List[string]

function Get-Sha256([string]$Path)
{
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-Size([string]$Path)
{
    (Get-Item -LiteralPath $Path).Length
}

# Download <url> <file>, three tries, so a dropped connection or a 404 reads
# as one clear line instead of an exception dump.
function Get-Url([string]$Url, [string]$OutFile)
{
    # -UseBasicParsing is what keeps Windows PowerShell from needing a working
    # Internet Explorer to fetch a file; PowerShell 7 has no such mode and
    # rejects the switch.
    $request = @{ Uri = $Url; OutFile = $OutFile; MaximumRedirection = 5; ErrorAction = 'Stop' }
    if ($PSVersionTable.PSEdition -eq 'Desktop') {
        $request.UseBasicParsing = $true
    }

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest @request
            return
        } catch {
            Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue
            if ($attempt -eq 3) {
                throw "error: download failed: $Url ($($_.Exception.Message))"
            }
            Start-Sleep -Seconds 2
        }
    }
}

# Download <url> and return its text, for the checksum files the releases
# publish beside their assets.
function Get-UrlText([string]$Url)
{
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("cadenza-" + [Guid]::NewGuid().ToString('N'))
    try {
        Get-Url $Url $temp
        Get-Content -LiteralPath $temp -Raw
    } finally {
        Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    }
}

# The hash a checksum file gives for <name>: "<hash>  <name>" (or "<hash>
# *<name>", the binary-mode form), one line per asset.
function Get-PublishedHash([string]$Sums, [string]$Name)
{
    foreach ($line in ($Sums -split "`n")) {
        $fields = ($line.Trim() -split '\s+') | Where-Object { $_ -ne '' }
        if ($fields.Count -lt 2) { continue }
        if (($fields[-1] -replace '^\*', '') -ne $Name) { continue }
        return $fields[0].ToLowerInvariant()
    }
    return $null
}

# True when <name> is already staged with the hash we expect, which lets a
# repeat run skip the network entirely.
function Test-Staged([string]$Name, [string]$Expected)
{
    $path = Join-Path $Dest $Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    (Get-Sha256 $path) -eq $Expected
}

# Moves an already-verified file into place and reports it. Callers are
# responsible for having checked the hash first.
function Install-Verified([string]$Name, [string]$Expected, [string]$Part)
{
    $final = Join-Path $Dest $Name
    Move-Item -LiteralPath $Part -Destination $final -Force
    $size = Get-Size $final
    Write-Host "ok $Name $Expected $size"
    $staged.Add("$Name $Expected $size")
}

function Write-Skipped([string]$Name, [string]$Expected)
{
    $size = Get-Size (Join-Path $Dest $Name)
    Write-Host "ok $Name $Expected $size"
    $staged.Add("$Name $Expected $size")
}

function Get-Ytdlp
{
    $expected = Get-PublishedHash (Get-UrlText "$YtdlpBase/SHA2-256SUMS") $YtdlpAsset
    if (-not $expected) {
        throw "error: $YtdlpAsset is missing from SHA2-256SUMS for $YtdlpVersion"
    }

    if (-not $Force -and (Test-Staged 'yt-dlp.exe' $expected)) {
        Write-Skipped 'yt-dlp.exe' $expected
        return
    }

    Write-Host "==> yt-dlp $YtdlpVersion"
    $part = Join-Path $Dest 'yt-dlp.exe.part'
    Get-Url "$YtdlpBase/$YtdlpAsset" $part
    $got = Get-Sha256 $part
    if ($got -ne $expected) {
        Remove-Item -LiteralPath $part -Force
        throw "error: yt-dlp.exe checksum mismatch (expected $expected, got $got)"
    }
    Install-Verified 'yt-dlp.exe' $expected $part
}

function Get-Qjs
{
    if (-not $Force -and (Test-Staged 'qjs.exe' $QjsSha256)) {
        Write-Skipped 'qjs.exe' $QjsSha256
        return
    }

    Write-Host "==> qjs $QjsVersion"
    $part = Join-Path $Dest 'qjs.exe.part'
    Get-Url "https://github.com/quickjs-ng/quickjs/releases/download/$QjsVersion/$QjsAsset" $part
    $got = Get-Sha256 $part
    if ($got -ne $QjsSha256) {
        Remove-Item -LiteralPath $part -Force
        throw "error: qjs.exe checksum mismatch (expected $QjsSha256, got $got)"
    }
    Install-Verified 'qjs.exe' $QjsSha256 $part
}

function Get-Ffmpeg
{
    if (-not $Force -and (Test-Staged 'ffmpeg.exe' $FfmpegExeSha256)) {
        Write-Skipped 'ffmpeg.exe' $FfmpegExeSha256
        return
    }

    Write-Host "==> ffmpeg $FfmpegTag"
    # The release's checksums first, so the archive is known good before the
    # download is paid for -- and so a replaced asset is caught even though
    # the tag is supposed to be frozen.
    $published = Get-PublishedHash (Get-UrlText "$FfmpegBase/checksums.sha256") $FfmpegAsset
    if ($published -ne $FfmpegArchiveSha256) {
        throw "error: $FfmpegAsset is published as $published, not $FfmpegArchiveSha256"
    }

    $archive = Join-Path $Dest 'ffmpeg.zip.part'
    Get-Url "$FfmpegBase/$FfmpegAsset" $archive
    $got = Get-Sha256 $archive
    if ($got -ne $FfmpegArchiveSha256) {
        Remove-Item -LiteralPath $archive -Force
        throw "error: ffmpeg archive checksum mismatch (expected $FfmpegArchiveSha256, got $got)"
    }

    $part = Join-Path $Dest 'ffmpeg.exe.part'
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
        try {
            # The build is one top-level folder -- ffmpeg-n8.1.3-win64-gpl-8.1
            # -- and only bin\ffmpeg.exe out of it is kept.
            $entry = $zip.Entries | Where-Object { $_.FullName -match '^[^/]+/bin/ffmpeg\.exe$' } | Select-Object -First 1
            if ($null -eq $entry) {
                throw "error: bin\ffmpeg.exe is not in $FfmpegAsset"
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $part, $true)
        } finally {
            $zip.Dispose()
        }
    } finally {
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    }

    $got = Get-Sha256 $part
    if ($got -ne $FfmpegExeSha256) {
        Remove-Item -LiteralPath $part -Force
        throw "error: ffmpeg.exe checksum mismatch (expected $FfmpegExeSha256, got $got)"
    }
    Install-Verified 'ffmpeg.exe' $FfmpegExeSha256 $part
}

Get-Ytdlp
Get-Qjs
if ($Ffmpeg) {
    Get-Ffmpeg
}

Write-Host "==> staged in $Dest"
$staged | ForEach-Object { Write-Host $_ }
