# Windows packaging

One command produces the file a Windows user downloads:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1
```

```
dist\cadenza-1.0.0-setup.exe              the installer: one file, everything inside
dist\cadenza-1.0.0-win64-portable.zip     with -Portable: unzip anywhere and run
dist\bin\                                 the application itself
```

The installer is per-user by default, so it does not ask for an administrator,
and it carries the Qt runtime and QML tree, libmpv, TagLib, the rest of the
MinGW runtime and the helper tools the music search runs. Nothing else has to be
installed on the machine for the app to work.

`.github/workflows/windows.yml` runs this same command -- push a tag, `v1.0.0`,
or start it by hand -- and uploads the installer and the portable zip as
artifacts, which is also where the DLL closure gets checked on a machine that
has never seen MSYS2.

## 1. Toolchain

[MSYS2](https://www.msys2.org/) with the UCRT64 environment packages all of it:

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
          mingw-w64-ucrt-x86_64-qt6-svg \
          mingw-w64-ucrt-x86_64-mpv mingw-w64-ucrt-x86_64-taglib \
          mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-ntldd \
          mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc
```

[Inno Setup](https://jrsoftware.org/isdl.php) 6.3 or newer is what turns
`dist\bin` into the installer. Without it the script stops after `dist\bin` and
says so, which is also what `-SkipInstaller` asks for.

MSVC and vcpkg work too -- `taglib` from vcpkg, `libmpv-2.dll` and its import
library from the [mpv Windows builds](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/),
which are GPL and so is this project -- but the script below is written for the
MSYS2 toolchain, which is the one that needs no assembly by hand.

## 2. What the build does

| Step | What it leaves behind |
|---|---|
| `packaging\tools\fetch-tools.ps1` | `packaging\tools\yt-dlp.exe`, `qjs.exe` and, unless `-SkipFfmpeg`, `ffmpeg.exe`, each verified against a pinned SHA-256 before it is renamed into place |
| `cmake` + `ninja` + `cmake --install` | `dist\bin\cadenza.exe` and `dist\bin\tools\` |
| `windeployqt` | the Qt DLLs, `platforms\`, `sqldrivers\`, `imageformats\` and the whole `qml\` tree |
| `ntldd -R` | `libmpv-2.dll`, `libtag-2.dll`, the MinGW runtime and the FFmpeg tree libmpv drags behind it |
| `ISCC` | `dist\cadenza-<version>-setup.exe` |

| Flag | What it changes |
|---|---|
| `-Msys2 <path>` | MSYS2 root, default `C:\msys64` |
| `-BuildDir <path>` / `-DistDir <path>` | default `<repo>\build-windows` and `<repo>\dist` |
| `-SkipTools` | no `tools\` at all: the music search says yt-dlp is missing |
| `-SkipFfmpeg` | tools without ffmpeg: downloads land in the container YouTube served, untagged |
| `-SkipInstaller` | stop after `dist\bin` |
| `-Portable` | also write the portable zip |

Two of those steps are worth understanding, because both fail silently rather
than loudly. `windeployqt` knows Qt and nothing else, and a missing QML module
is a blank window with no error at all -- which is why the `qml\` tree has to
travel, `QtQuick\Controls\Basic` in particular, the style `main.cpp` asks for.
The Qt plugins come from the same step, and it deploys a plugin when the module
it belongs to is in use: of the platform plugins that means `qwindows.dll` and
nothing else, and `--add-plugin-types imageformats,tls` is passed by name
because the two it names are the difference between working and looking like it
-- no JPEG decoder is a library where every cover is blank, and no TLS backend
is a lyrics lookup that never answers. `ntldd -R` walks the closure of the
executable, which is the only dependable way to learn what is missing: libmpv is
a small DLL in front of FFmpeg, libass, fontconfig and harfbuzz, MinGW programs
need `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll` beside
them, and a missing one of those is a program that will not start and a Windows
error dialog that says nothing useful. It is also why no file name in this
script mentions taglib: the closure finds `libtag-2.dll` (taglib 2.x names it
that) the way the loader will.

### By hand

```powershell
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64
cmake --build build-windows
cmake --install build-windows --prefix dist
C:\msys64\ucrt64\bin\windeployqt.exe --release --no-translations --qmldir src\ui dist\bin\cadenza.exe
```

and then the DLL closure the script does with `ntldd`, and
`ISCC.exe /DAppVersion=1.0.0 /DSourceDir=...\dist\bin /DOutputDir=...\dist packaging\windows\cadenza.iss`.

## 3. Why an installer and not one .exe

An executable with nothing beside it means Qt linked statically: a Qt built from
source with `-static`, its QML modules pulled in through `qt_import_qml_plugins`,
libmpv and TagLib from static triplets, and yt-dlp, QuickJS and ffmpeg embedded
in the resource system only to be written out to disk on first run -- they are
child processes, and a process needs a file. That is the only way to a single
file with no install, and it is a project of its own.

What this repository ships instead is the ordinary Windows answer: the one file
a user downloads is the installer, and what it installs is a folder it owns.
`-Portable` writes the same folder as a zip, for a USB stick or a machine that
should not be touched; a self-extracting wrapper around it (7-Zip SFX, Enigma
Virtual Box) would make that one file again, at the price of antivirus
heuristics and a launcher that unpacks into `%TEMP%` before it starts.

The sizes are the reason ffmpeg can be left out: `ffmpeg.exe` is 163 MB
unpacked, `yt-dlp.exe` 18 MB and `qjs.exe` 2 MB, and lzma2 shrinks those by
rather less than half. `-SkipFfmpeg` is the difference between a download that
takes a couple of minutes and one that takes seconds.

The index is not part of any of this: it lives in
`%LOCALAPPDATA%\cadenza\library.db` and outlives an uninstall, which is the
point -- reinstalling should not rescan a library.

## 4. Signing

An unsigned installer triggers SmartScreen, which shows a full-screen "Windows
protected your PC" warning until enough people have run it. Removing that
requires an EV code-signing certificate and `signtool`:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /f cert.pfx /p PASSWORD dist\bin\cadenza.exe
```

Sign before running `ISCC`, so the executable inside the installer carries the
signature too.

## 5. Helper tools

The music search runs yt-dlp, a JavaScript runtime and ffmpeg as child
processes, and looks for them in `tools\` next to `cadenza.exe` before it looks
on `PATH`. `packaging\tools\fetch-tools.ps1` stages them:

| Tool | Asset |
|---|---|
| `yt-dlp.exe` | [yt-dlp release](https://github.com/yt-dlp/yt-dlp/releases), `yt-dlp.exe`, checksum from `SHA2-256SUMS` |
| `qjs.exe` | [quickjs-ng release](https://github.com/quickjs-ng/quickjs/releases), `qjs-windows-x86_64.exe` |
| `ffmpeg.exe` | [BtbN builds](https://github.com/BtbN/FFmpeg-Builds/releases) of FFmpeg, win64-gpl, from a dated autobuild tag; only `bin\ffmpeg.exe` is kept |

It is the Windows half of `packaging/tools/fetch-tools.sh`, with the same rules:
versions pinned, every download verified against a SHA-256 before it lands, and
a correct file already staged means no network at all. Keep `yt-dlp` current --
the build shipped by the distributions is old enough to be answered with HTTP
403.

## 6. Icon

`packaging/cadenza.svg` is the only drawing the project keeps: the window and
the sidebar render it out of the copy inside the binary, and an installed
desktop shows it through the `.desktop` file. An `.exe` is not a `.desktop`
file, so `cadenza.exe` itself still wears the stock Windows icon, and so do its
shortcuts. Changing that means an `.ico` built from the same SVG at 16, 32, 48
and 256 pixels, a one-line `.rc` compiled into the executable, and
`SetupIconFile=` in `cadenza.iss`.
