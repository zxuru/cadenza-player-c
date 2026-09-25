# Cadenza

A local-files music player. Point it at a folder, it indexes what is there and
plays it. No streaming, no account, and no network until you ask for one: the
library is local files, and the only two things that ever open a socket are the
lyrics lookup you switch on and the music search you type into -- the second one
through `yt-dlp`, which the app runs and the packages ship.

This is a native rewrite of [cadenza-player](../cadenza-player), which was
written in Python on top of Flet and GStreamer.

## Why the rewrite

The Python version works, but Flet runs **two** runtimes in **two** processes —
a CPython interpreter and a full Flutter engine — talking to each other over a
local socket with JSON. Measured on the reference machine, same session, same
library:

| | Flet / Python | Cadenza (C++/Qt6) |
|---|---|---|
| Resident memory | 250 MB (56 MB Python + 194 MB Flutter) | 184 MB |
| Processes | 2 | 1 |
| Time to first frame | >1 s | 252–284 ms |
| Runtime shipped | 49 MB Flet client + 27 MB venv | 3.2 MB binary |
| Threads | — | 17 |

The same binary measures 109 MB RSS and 110 MB PSS with the scene graph on the
software backend, so roughly 75 MB of the desktop figure is the platform plugin
and GPU driver rather than Cadenza itself.

The audio engine is not the problem: GStreamer is already C, and `playbin`
decodes exactly as well from Python as from C. What the rewrite buys is startup
time, a single process, a 3 MB binary instead of 76 MB of runtime, and direct
control over the playback path.

## What it does

- Recursive folder indexing with TagLib tag reading, into a SQLite index.
- Incremental rescans: a file is re-probed only when its `(mtime, size)` pair
  changed, so re-indexing an unchanged library is instant.
- Gapless playback across an album, and ReplayGain -- off, per track or per
  album, switched beside the volume in the transport -- both provided by libmpv
  rather than hand-written.
- **Browsing: songs, artists, albums or genres.** The pill before the search
  box says what the list below is a list of, and opens the list of modes. In
  `Songs` it is every track in album order; in `Artists`, `Albums` or `Genres`
  it is one row per artist, album or genre -- forty thousand files become the
  twelve artists they belong to, each with the artwork of one of its tracks
  standing for the whole group, how many tracks and albums are in there, and
  how long it all runs. Opening a row narrows the list to that group's tracks,
  with a chip above the list as the way back out. The search box narrows
  whatever is showing: in a grouped mode it matches the names being listed --
  `coltrane` keeps the albums of Coltrane, because an album row names its
  artist too -- and inside a group it matches every field of the tracks in
  there, since the group has already settled the rest. Matching ignores case
  **and accents**, so `cancion` finds `Canción`.
- **Folder playlists.** The subfolders of the music folder appear in the
  sidebar as playlists with their track counts; picking one restricts the
  library to that whole subtree, so an `Artist/Album/` layout reads as one
  playlist per artist. The search box then searches inside that playlist. The
  folder you were last in, and the music folder itself, are remembered across
  launches. Files sitting loose in the root belong to no playlist and appear
  under "All music". A playlist wears a pencil when the pointer is over it, and
  answers a double click with it: the name is edited in place, and renaming one
  renames the folder -- the index, the selection, where downloads land and the
  queue playing out of it all follow, so a playlist can be renamed mid-album
  without the music stopping. The heading over the playlists carries two faint
  buttons: the first shows the music folder in the desktop's own file manager,
  for whatever is done to a file outside the player, and the second walks it
  again -- incrementally, the same `(mtime, size)` test as any other rescan --
  so a file that arrived from outside is in the index and the rail without a
  relaunch. The second turns while the walk runs.
- **Shuffle, in one of two pools.** Inside the list that is playing -- the
  playlist or the search results playback started from, remembered while it
  plays, so browsing to another playlist does not change it -- or inside the
  whole library. The button beside the transport's prev/next steps through off,
  the list and the library; the fill says which of the three is on, and the
  tooltip names the list it would deal from. Changing the mode never touches the
  track that is playing: what is ahead of it is dealt again, what is behind it
  stays where it was, so turning shuffle off walks the list's own order from
  where the playhead is. The mode itself is remembered across launches.
- Embedded cover art, extracted per format (FLAC blocks, ID3 APIC frames, MP4
  `covr` atoms, Ogg `METADATA_BLOCK_PICTURE`).
- **Lyrics**, looked for from the cheapest source first: a `.lrc` (or `.txt`)
  file beside the audio file, then the track's own tags (`SYLT`, `USLT`,
  `LYRICS`, `©lyr`), then -- only if you turn online lookups on -- the
  [LRCLIB](https://lrclib.net) free lyrics API, asked the track exactly and
  then, when it does not know that spelling of it, asked again a little more
  loosely: a file whose artist tag names every voice in the track -- which is
  what a download from YouTube or the Internet Archive carries -- is filed
  under no artist any database could match. Whatever answers is cached in the
  index, so a track is asked about once. When the lyrics carry timings, the
  line being sung is picked out and the view follows the playhead; clicking a
  line seeks to it. Timings inside the file (`SYLT`) win over anything fetched.
- Bit-perfect output through mpv's `audio-exclusive`, switched by the
  `EXCLUSIVE` toggle in the transport: the stream takes the device over, so
  nothing in the system resamples or mixes it. WASAPI on Windows, CoreAudio on
  macOS and PipeWire on Linux implement it; every other output ignores the
  flag.
- **Getting music.** Search [YouTube Music](https://music.youtube.com) from
  inside the player and download what you pick. The view opens with the caret
  already in the search box, so a query is one keystroke away: `Enter`, or the
  `Search` button at the end of the row, is what asks -- this box is not a
  filter that fires as it is typed into, unlike the one in the library. Every
  result comes back with its cover, its artist and its year, releases first --
  the cover being what says at a glance which of two results with the same
  title is the one. The files land in the folder the sidebar has selected --
  the music folder, or the playlist folder you are in -- tagged with the title,
  the artist, the album and the cover, and are in the library as they arrive. A
  release arrives whole, in a folder of its own, which is also a playlist here.
- **Its own window chrome on every system.** No title bar is delegated to the
  platform: the window is frameless, rounded and translucent, and it owns
  everything a title bar does. The band across its top, and the application's
  own name, drag it; double-clicking them maximizes; the minimize, maximize and
  close controls sit at the right end of the first row of whichever view is
  showing -- next to the search field in the library, next to the lyrics toggle
  in the hero, and alone on the settings page -- drawn in the same material as
  the panes, in the same place on Linux, Windows and macOS.

### Interface

The interface is one material, three colours and whatever is playing.

The material is glass: the panes (the navigation rail, the transport, the
search field) are translucent, with a one-pixel light border and a sheen across
their top edge. What shows through them is the window's own pane: the cover of
the track that is playing, blurred, over a violet wash that stands in when
nothing is. The result is that the window is lit by the record -- an album of
blue cover art lights it blue -- so the colour on screen is content rather than
decoration. The rest of the palette is violet (`Theme.accent`), the accent for
everything that is on, playing or chosen, and it is defined once in
`src/ui/Theme.qml`, which is also the only place a radius, a spacing step or a
type size is written down.

**The icon is that material, in one file.** `packaging/cadenza.svg` is a pane of
the same glass -- violet over the void, lit from the top left, with the record's
bloom behind it -- carrying a quaver whose flag is a cadenza: the flourish a
soloist sweeps at the end of a movement, curling down to the dot the interface
punctuates its own name with, which is why the mark sits beside "Cadenza" in the
rail. The launcher reads it from `hicolor/scalable/apps`; the build binds the
same file into the binary, where the window's icon is rendered from it and the
rail draws it. Dock, window and rail are then one drawing rather than three that
have to be kept in step, and the icon cannot be the half of the application that
was left behind when the rest of it changed.

**Three views in the rail, and one door out of the transport.** The navigation
rail holds the library, the music search and the settings page, and under them
the playlists: it carries nothing else -- no counts, no stream details, no
switch that is set once and then left alone -- beyond the two faint glyphs on
the playlists' heading, which show the music folder and walk it again. The
hero view is not one of its rows: the track in the transport is already the
way into it, so clicking the artwork there opens it, with the pointer and a
tooltip saying so. The settings page (`src/ui/Settings.qml`) collects what the
rail used to carry at its foot:
the music folder with its track count and the line about what the last walk
did, the lyrics -- the pane, off until it is asked for, and the online lookup
-- and the language.

Two consequences are worth knowing:

**The backdrop's blur and its corners are baked into the image**, by
`CoverArtProvider`, rather than applied by a shader on top of it. A shader is
not a safe place for anything that has to be there: on a backend where effects
cannot run (the software renderer, a machine with no usable GPU driver, a
remote desktop) the masked item does not draw at all, and the artwork would
disappear with it. Baked in, the interface asks for no effect anywhere -- it
draws on the software renderer exactly as it does on a GPU. The window hands
the provider the radius it is drawing with, so the picture's corners and the
frame's rounding cannot drift apart.

**Rounding an image is not a `DestinationIn`.** A composition mode only reaches
the pixels the drawn shape covers, so compositing a rounded rectangle into the
picture keeps the picture's own square corners. The mask is drawn first and the
picture composited into it, which also keeps the corner edges antialiased.

**The window fills its own rectangle.** The rounding is cut into the surface
itself and the only line around it is the border of the glass, so there is one
edge and no transparent margin beside it -- a margin big enough for a shadow
reads as a second border, and the resize grips fit inside the window's edge
just as well.

The window is translucent, so it wants a compositor: under X11 without one the
corners come out black instead of showing the desktop. Wayland, and every
platform's default compositing setup, is unaffected.

### Lyrics and the network

The lyrics pane is **off by default** -- a column of text over half the view is
not something to have on without saying so -- and is switched on either from
the settings page or from the `LETRA` pill in the hero, which is the pill that
also takes it away.

The online lookup is **off by default** too, and the player opens no socket for
lyrics until you switch it on: from the settings page, or from the pill at the
foot of the lyrics pane, which is where its absence is felt. It sends LRCLIB
the track's title, artist, album and duration -- nothing else, no key, no
account -- and honours the `Retry-After` the API asks for. What comes back is
stored in `lyrics` in the index (`$XDG_DATA_HOME/cadenza/library.db`); that
table is the only thing the network ever writes.

A lookup asks up to three questions, each a little looser than the last, and
stops at the first one answered: the track exactly as tagged; then the same
track with the artist cut down to the first name of a multi-artist credit and
the album left out, since an album tag is as likely to be a folder name as a
release; then LRCLIB's loose search on the title alone, whose candidates are
taken only when the title matches word for word and the run time is the
track's, within the two seconds the API's own exact match allows. Only a
refusal moves a lookup on: a timeout or a `429` is a fact about the network
rather than about the track, and is simply tried again the next time it plays.

### Getting music

The library is local files, and **Get music** is how they get there without
leaving the window. It searches [YouTube Music](https://music.youtube.com)
through [yt-dlp](https://github.com/yt-dlp/yt-dlp): one flat search of
`music.youtube.com/search` for the page, one yt-dlp run per candidate to find
out what each one is, and yt-dlp again for the download itself -- with ffmpeg
writing the title, the artist, the album and the cover into the file. The search
happens when you ask for it, which is why this one is not behind a switch the
way the lyrics lookup is.

**Releases and songs, in that order.** YouTube Music ranks by popularity, so the
top hit for a song is often a re-upload by an unrelated channel; the search
answers with one row per candidate, resolved first, and what carries music
metadata -- a release, or an upload with an artist and an album on it -- comes
before what does not, each group keeping YouTube's own order. Every row carries
the cover YouTube Music shows for it, asked for at the size a row draws rather
than as the master: two results can wear the same title, and the picture is what
tells them apart before anything is downloaded. A release is a row like any
other: picking it fetches its whole track list, into a folder of its own,
numbered in the release's order.

**The tools travel with the app.** `yt-dlp` searches and downloads, a JavaScript
runtime (a bundled `quickjs`, or whatever the machine already has) deciphers
YouTube's stream URLs, and ffmpeg converts the audio and writes its tags. The
app looks for them in `tools/` next to the executable first -- which is where
the packages put them -- and on `PATH` after that.
`packaging/tools/fetch-tools.sh` downloads the pinned Linux versions into
`packaging/tools/`, checks each one's SHA-256 and stages it for the build, and
`packaging/tools/fetch-tools.ps1` does the same for the Windows ones.
Without ffmpeg the audio is saved in the container YouTube served, untagged;
without yt-dlp the view says so instead of searching.

**A stopped download takes its pieces with it.** yt-dlp writes `<name>.part`
while it works, and stopping one deletes those, the state file beside them and
the folder a release had made for itself, once nothing else is in it -- half a
track never stays in the folder the library watches. What had already finished
stays.

**Where it lands is the sidebar's selection**, which the view says above the
results: the music folder itself under "All music", and the playlist folder when
one is picked -- which is also how a download becomes a playlist. A release gets
a folder of its own inside it.

What is downloaded is what YouTube serves to an anonymous client, and the same
rules apply to it as in a browser: no account, no cookie jar, and nothing here
decides for you what may be copied.

### Languages

Every string the interface shows comes from a locale file, one per language,
in `locales/<code>.json` -- ISO 639-1, plus a region only when it differs from
the base language (`en.json`, `es.json`, `pt-BR.json`). A file is a flat JSON
object, and a key holds either the finished string:

```json
"get_music_search": "Search",
```

or, when the text depends on a count, its forms:

```json
"library_tracks": { "one": "1 track", "many": "{count} tracks" }
```

`{name}` placeholders are filled from what the call passes, and only the forms
a language actually needs have to be written: anything a file leaves out --
anything it gets wrong, too -- falls back to English, so a half-finished
translation is usable rather than broken.

**Dropping a file in is the whole registration step.** It goes in `locales/`
beside the executable, where it wins over the copy compiled into the binary,
and it appears in the language list on the settings page under the name it
gives itself (`"language_name"`). Nothing else keeps a list of languages: not
the code, not the build.

The language in force is the one the system is set to -- `LANG`, `LC_ALL`, or
the platform's own setting, with a regional variant served by its base
language, so `es-AR` reads `es.json` -- until one is picked on the settings
page, which is then remembered. Switching redraws the interface where it
stands: everything on screen, down to the status line above the folder it
describes and a download's progress, travels as keys rather than as finished
sentences, so it is re-read in the new language instead of staying in the old
one.

`cadenza --check-i18n` holds every locale against English and exits non-zero
when one is incomplete: what it is missing, what English does not have, what
will not parse. Name languages to check only those. It reads JSON and nothing
else, so it wants no window and no library.

## Dependencies

libmpv does the decoding (through FFmpeg), the device output (WASAPI on
Windows, CoreAudio on macOS, ALSA/PulseAudio/PipeWire on Linux) and owns the
queue. TagLib reads tags. Qt 6 draws the interface and holds the index, and
its own network stack is what reaches LRCLIB for lyrics the file and its
folder could not answer for. The music search is not Qt's: it runs `yt-dlp`
and ffmpeg as child processes, out of `tools/` beside the executable, which is
the only work the player hands to something that is not this binary. Qt's
SVG module is the one addition of its own, and it is there for the icon: the
window's icon is rendered from `packaging/cadenza.svg`, out of the copy the
build binds into the binary.

| | Minimum |
|---|---|
| Qt | 6.5 (developed against 6.10) |
| libmpv | 0.37 |
| TagLib | 1.13 |
| Compiler | C++20 |

The three tools the music search runs are not build dependencies: they are
looked for in `tools/` beside the executable first and on `PATH` after that, so
a build from source runs with whatever the machine has.
`packaging/tools/fetch-tools.sh` stages the pinned `yt-dlp` and QuickJS (and
ffmpeg behind `--ffmpeg`) into `packaging/tools/`, which the build symlinks into
`build/tools/`, an install copies to `<prefix>/bin/tools/`, and the AppImage
script bundles. `packaging/tools/fetch-tools.ps1` is the same thing for the
Windows builds, whose staged names carry the `.exe` the app looks for there.
An old `yt-dlp` is worse than none: 2026.03.17 answers audio requests with HTTP
403, which is why the version is pinned.

### Debian / Ubuntu

```bash
sudo apt install build-essential cmake pkg-config \
    qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-svg-dev \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates \
    qml6-module-qtquick-window qml6-module-qtquick-effects \
    qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript \
    libmpv-dev libtag1-dev libsqlite3-dev
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
    mpv-libs-devel taglib-devel sqlite-devel
```

### Arch

```bash
sudo pacman -S base-devel cmake pkgconf qt6-base qt6-declarative qt6-svg \
    mpv taglib sqlite
```

### macOS

```bash
brew install qt mpv taglib pkg-config cmake ninja
```

### Windows

See [packaging/windows/README.md](packaging/windows/README.md).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/cadenza
```

Pass a folder to index somewhere other than the default:

```bash
./build/cadenza ~/Music
```

Index a library without opening a window — useful from a script or a timer:

```bash
./build/cadenza --scan ~/Music
```

`--database <path>` points the index somewhere other than the default
location, which is `$XDG_DATA_HOME/cadenza/library.db` on Linux,
`%LOCALAPPDATA%\cadenza\library.db` on Windows and
`~/Library/Application Support/cadenza/library.db` on macOS.

## Packaging

| Platform | Artifact | Guide |
|---|---|---|
| Linux | `.AppImage` (one file, no install) | [packaging/linux/build-appimage.sh](packaging/linux/build-appimage.sh) |
| Linux | `.deb` / `.rpm` | `cd build && cpack -G DEB` (CPack is already wired up) |
| Windows | `.exe` installer, one file with everything inside | [packaging/windows/README.md](packaging/windows/README.md) |
| macOS | `.dmg`, signed and notarised | [packaging/macos/README.md](packaging/macos/README.md) |

The AppImage is the one to reach for on Linux: it bundles Qt, libmpv and
TagLib, so it runs on a distribution that has none of them installed. A
Flatpak is possible but is a much larger undertaking here, because the KDE
runtime does not ship libmpv and it would have to be built along with its
FFmpeg dependency inside the sandbox.

## Releases

A release is a tag, and everything after that is the workflows' job:

```bash
# The tag and the VERSION in CMakeLists.txt carry the same number.
git tag v1.0.1
git push origin v1.0.1
```

The tag starts `.github/workflows/release.yml`, which

1. refuses to build when the tag and `VERSION` in `CMakeLists.txt` disagree,
   because the tag is what the filenames, the installer and `--version` will
   say;
2. builds the Windows installer and portable zip (`.github/workflows/windows.yml`,
   on `windows-latest` with MSYS2) and the AppImage
   (`.github/workflows/linux.yml`, on `ubuntu-24.04`, with Qt fetched from the
   Qt project since no Ubuntu package meets the 6.5 floor);
3. tests what was built rather than only that it built — the installer is
   installed silently and the installed copy is opened, and the AppImage is
   unpacked and run with a scrubbed environment, so it has to find the Qt,
   libmpv, TagLib and helper tools inside itself;
4. publishes a GitHub release named after the tag with those three files and a
   `SHA256SUMS` beside them, then reads the release back to check that all four
   landed.

Both platform builds also run by hand from the Actions tab, which is how to get
an artifact without a tag; a hand run of `release.yml` does everything a tag
does except publish, the check of what was built included. macOS is the one
platform with no CI: signing and notarising need a paid Apple account, so
`packaging/macos/README.md` is the manual route.

The AppImage links the glibc of the machine that built it — Ubuntu 24.04, glibc
2.39 — so it wants a 2024-or-newer distribution; Qt, libmpv, TagLib and the
helper tools are inside the file. Pushes to `main` and pull requests run the same
Linux job without ffmpeg, which is the fast half of a release.

## Architecture

```
src/main.cpp              entry point, CLI, QML engine setup
src/core/
  Player.{h,cpp}          libmpv wrapper: queue, transport, properties
  Library.{h,cpp}         SQLite index, read-only, GUI thread
  Scanner.{h,cpp}         filesystem walk + TagLib, worker thread
  TrackModel.{h,cpp}      QAbstractListModel over the index, and the folded
                          text a search and a grouping read
  TrackFilterProxy.{h,cpp} accent-insensitive search, restricted to one folder,
                          to one field of a track, and to one group
  GroupModel.{h,cpp}      those tracks as one row per artist, album or genre
  CoverArtProvider.{h,cpp} image://cover/...: decode cache, plus the cropped,
                          blurred and rounded variants the backdrop and the
                          thumbnails ask for by URL
  Lyrics.{h,cpp}          LRC and plain-text parsing, and the line at a time
  LyricsModel.{h,cpp}     those lines as a list model, plus the active one
  LyricsProvider.{h,cpp}  sidecar -> tags -> LRCLIB, cached in the index
  Downloader.{h,cpp}      search YouTube Music through yt-dlp, and download
                          what you pick into the folder the sidebar picked
  DownloadModel.{h,cpp}   those results as a list model, each row carrying the
                          state of its own download
  Translator.{h,cpp}      the locale files, the system's language, and the
                          strings every other part of the interface reads
  AppController.{h,cpp}   the single object QML sees
locales/                  one JSON file per language, compiled into the binary
                          and read from beside it first
src/ui/                   QML, no logic beyond presentation. The window's own
                          chrome is here too: Main.qml (frameless window, drag
                          band, resize grips), Backdrop.qml (the lit pane),
                          Glass.qml (the one pane material), TitleBar.qml
                          (the controls drawn where a title bar would be),
                          Sidebar.qml (the rail), TransportBar.qml (the bar,
                          and the artwork that opens the hero), LibraryBrowse.qml
                          (what the library list is a list of), GroupRow.qml
                          (one artist, album or genre of it), and Tr.qml (the
                          strings, read through the singleton so a binding
                          redraws when the language changes)
```

Four decisions worth knowing:

**SQLite access is split by thread, not shared.** `Library` owns the GUI-thread
connection, `Scanner` opens its own on its worker thread. Neither ever touches
the other's handle, so there is no cross-thread SQLite state to get wrong.

**mpv owns the queue.** `Player::load` writes the playlist into mpv and plays
by index rather than tracking an index of its own. That is what gapless
playback consumes, so the two cannot drift apart. `Player` keeps a copy of the
order for one thing only: `reorderTail`, which replaces the entries after the
playhead -- never the one it is on -- which is how a shuffle mode changes
without interrupting the track that is playing, and how a queue held together
from another list goes back to its own order.

**The library is read once, and only when the index has moved.** The index is
held in memory as a list of tracks -- roughly 1.3 KB each, so fifty thousand of
them are about 65 MB -- and every search, grouping, count and column is
answered from that copy. A rescan that re-read nothing and pruned nothing
leaves it alone instead of reading fifty thousand rows back, folding them and
resetting the view: the scanner reports what it re-read and what it removed,
and that is what decides. Measured on fifty thousand files: the walk itself is
about 300 ms, reading and folding the library back is about 165 ms, a keystroke
is 5 ms (10 ms while a grouped mode is showing, which also regroups), and
switching to artists, albums or genres is 6-16 ms. The view is a `ListView`
with reused delegates, so what is on screen costs what is on screen rather than
what is in the library.

**Lyrics are read where they are cheap and stored where they are not.** A
sidecar file and the track's own tags are re-read on every track change, so an
edited `.lrc` shows up without a rescan; only a network answer is written to
the index, because only a network answer is expensive to get again. The file
read happens on a worker thread, the way the scanner's does, and the SQLite
table it lands in is touched from the GUI thread alone. Each cached answer
records the version of the lookup that wrote it, so improving the questions
retires what the earlier ones answered -- otherwise a file remembered as having
no lyrics would stay that way through every later fix.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

libmpv is GPLv2+ as distributed by Linux distributions and by the official
Windows builds; Qt 6 is LGPLv3; TagLib is LGPLv2.1/MPL. All are compatible
with a GPL-3.0 application.
