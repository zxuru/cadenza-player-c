# macOS packaging

## 1. Dependencies

```bash
brew install qt mpv taglib pkg-config cmake ninja
```

Homebrew's `mpv` is built as LGPL and ships `libmpv.dylib`, which matches this
project's licence.

## 2. Universal build

Apple silicon and Intel Macs are separate architectures. A universal binary
avoids shipping two downloads:

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES` only works if libmpv and TagLib are themselves
universal. Homebrew ships single-architecture bottles, so a true universal
build needs the dependencies built from source with the same flag, or a
`lipo` of two Homebrew prefixes. If that is more trouble than it is worth,
build and ship `arm64` only — Intel Macs are a shrinking share.

## 3. Bundle the frameworks

`macdeployqt` walks the executable's dependencies and rewrites the install
names to point inside the bundle, which is what makes the app self-contained:

```bash
cmake --install build --prefix dist
macdeployqt dist/Cadenza.app -qmldir=src/ui -always-overwrite
```

Check that `dist/Cadenza.app/Contents/Frameworks/` contains `libmpv.*.dylib`
and `libtag.*.dylib` afterwards. If they are missing, `macdeployqt` did not
follow them — copy them in and re-run `install_name_tool` by hand.

## 4. Disk image

```bash
brew install create-dmg
create-dmg \
    --volname "Cadenza" \
    --window-size 600 400 \
    --icon-size 110 \
    --icon "Cadenza.app" 150 190 \
    --app-drop-link 450 190 \
    "dist/cadenza-1.0.0.dmg" \
    "dist/Cadenza.app"
```

## 5. Signing and notarisation

This step is not optional in practice. Without it, Gatekeeper refuses to open
the app on any Mac that did not build it, and the user sees "Cadenza is damaged
and can't be opened" — a message that says nothing about the real cause.

Requires a paid Apple Developer account (99 USD/year).

```bash
# Sign inside-out: every nested binary first, then the bundle.
codesign --force --options runtime --timestamp \
    --sign "Developer ID Application: Your Name (TEAMID)" \
    dist/Cadenza.app/Contents/Frameworks/*.dylib

codesign --force --options runtime --timestamp \
    --sign "Developer ID Application: Your Name (TEAMID)" \
    dist/Cadenza.app

# Notarise: upload, wait for Apple to scan it, then staple the ticket so the
# app validates even offline.
xcrun notarytool submit dist/cadenza-1.0.0.dmg \
    --apple-id "you@example.com" \
    --team-id "TEAMID" \
    --password "@keychain:AC_PASSWORD" \
    --wait

xcrun stapler staple dist/cadenza-1.0.0.dmg
```

Verify before publishing:

```bash
spctl --assess --type open --context context:primary-signature -v dist/cadenza-1.0.0.dmg
```

## Helper tools

The music search runs `yt-dlp`, a JavaScript runtime and ffmpeg as child
processes, and looks for them in `tools/` next to the executable before it
looks on `PATH`. Inside a bundle that is
`Cadenza.app/Contents/MacOS/tools/`, so the tools go there -- and each one has
to be signed, because a notarised app may not carry an unsigned executable:

| Tool | Asset |
|---|---|
| `yt-dlp` | [yt-dlp release](https://github.com/yt-dlp/yt-dlp/releases), `yt-dlp_macos` |
| `qjs` | [quickjs-ng release](https://github.com/quickjs-ng/quickjs/releases), `qjs-darwin-*` |
| `ffmpeg` | a static macOS build for both architectures; without it the audio is saved untagged, in the container YouTube served |

Sign them before the bundle itself, so the seal covers what is inside it:

```bash
for tool in Cadenza.app/Contents/MacOS/tools/*; do
    codesign --force --options runtime --timestamp \
        --sign "Developer ID Application: Your Name (TEAMID)" "$tool"
done
```

`packaging/tools/fetch-tools.sh` stages the Linux pair only and refuses to run
elsewhere, so the macOS files are fetched by hand today. Pin the versions the
way that script pins them, and keep `yt-dlp` current: an old one is answered
with HTTP 403.
