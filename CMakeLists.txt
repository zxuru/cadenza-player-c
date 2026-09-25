cmake_minimum_required(VERSION 3.21)

project(cadenza
    VERSION 1.0.0
    DESCRIPTION "A local-files music player"
    HOMEPAGE_URL "https://github.com/zxuru/cadenza"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# QuickControls2 supplies the styled controls, Concurrent runs the scanner off
# the GUI thread, Sql backs the library index, Network is what reaches LRCLIB
# for the lyrics the local sources could not answer for, and Svg is what draws
# the one icon the launcher, the window and the sidebar share.
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Concurrent
    Network
    Sql
    Svg
)
qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)

# Player spawns its event loop with std::thread. glibc 2.34+ folds pthread into
# libc, but older toolchains and macOS still need the flag.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# libmpv: decoding, device output and the queue.
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

# TagLib ships a CMake config on some platforms and only a .pc file on others.
find_package(TagLib QUIET CONFIG)
if(TagLib_FOUND)
    set(CADENZA_TAGLIB TagLib::TagLib)
else()
    pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
    set(CADENZA_TAGLIB PkgConfig::TAGLIB)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

set(CADENZA_SOURCES
    src/main.cpp
    src/core/AppController.cpp
    src/core/CoverArtProvider.cpp
    src/core/Downloader.cpp
    src/core/DownloadModel.cpp
    src/core/GroupModel.cpp
    src/core/Library.cpp
    src/core/Lyrics.cpp
    src/core/LyricsModel.cpp
    src/core/LyricsProvider.cpp
    src/core/Player.cpp
    src/core/Scanner.cpp
    src/core/Track.cpp
    src/core/TrackFilterProxy.cpp
    src/core/TrackModel.cpp
    src/core/Translator.cpp
)

# One file per language, and the list is the directory: dropping a locale in
# registers it, here and at runtime, with nothing to keep in step.
file(GLOB CADENZA_LOCALES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/locales/*.json")

set(CADENZA_QML
    Theme.qml
    Tr.qml
    Main.qml
    Backdrop.qml
    Glass.qml
    TitleBar.qml
    Sidebar.qml
    LibraryView.qml
    TrackRow.qml
    GroupRow.qml
    NowPlaying.qml
    LyricsPane.qml
    GetMusic.qml
    Settings.qml
    DownloadRow.qml
    TransportBar.qml
    SeekBar.qml
    VolumeControl.qml
    IconButton.qml
    TogglePill.qml
    LibraryBrowse.qml
    EmptyState.qml
    Marquee.qml
)

foreach(qml IN LISTS CADENZA_QML)
    # Flatten the resource tree: without this the module would be
    # Cadenza.src.ui and every import would need the full path.
    set_source_files_properties(src/ui/${qml} PROPERTIES QT_RESOURCE_ALIAS ${qml})
    list(APPEND CADENZA_QML_FILES src/ui/${qml})
endforeach()

set_source_files_properties(src/ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(src/ui/Tr.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The locale files travel inside the binary, so an archive of it is the whole
# application; the copy beside it is what a translation is edited in, and the
# one the executable reads first.
foreach(locale IN LISTS CADENZA_LOCALES)
    get_filename_component(locale_name "${locale}" NAME)
    set_source_files_properties("${locale}" PROPERTIES QT_RESOURCE_ALIAS "${locale_name}")
endforeach()

qt_add_executable(cadenza ${CADENZA_SOURCES})

qt_add_resources(cadenza "locales"
    PREFIX "/locales"
    FILES ${CADENZA_LOCALES}
)

# The icon travels inside the binary as well as being installed for the
# launcher: `main.cpp` builds the window's icon from this copy and the sidebar
# shows it, so the three can never be different drawings.
set_source_files_properties(packaging/cadenza.svg PROPERTIES QT_RESOURCE_ALIAS cadenza.svg)

qt_add_resources(cadenza "icons"
    PREFIX "/icons"
    FILES packaging/cadenza.svg
)

qt_add_qml_module(cadenza
    URI Cadenza
    VERSION 1.0
    QML_FILES ${CADENZA_QML_FILES}
)

target_include_directories(cadenza PRIVATE src)

target_compile_definitions(cadenza PRIVATE CADENZA_VERSION="${PROJECT_VERSION}")

target_link_libraries(cadenza PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
    Qt6::Svg
    Threads::Threads
    PkgConfig::MPV
    ${CADENZA_TAGLIB}
)

if(MSVC)
    target_compile_options(cadenza PRIVATE /W4 /permissive-)
else()
    target_compile_options(cadenza PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Platform packaging
# ---------------------------------------------------------------------------

set_target_properties(cadenza PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Cadenza"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cl.zxuru.cadenza"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

install(TARGETS cadenza
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)

# Beside the binary in the build tree, and under `share` where an installed one
# looks for them: either way the file on disk wins over the copy in the binary.
add_custom_command(TARGET cadenza POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/locales" "$<TARGET_FILE_DIR:cadenza>/locales"
    COMMENT "Copying the locale files next to the executable"
)

install(FILES ${CADENZA_LOCALES}
    DESTINATION share/cadenza/locales
)

install(FILES packaging/cadenza.desktop
    DESTINATION share/applications
    OPTIONAL
)

install(FILES packaging/cadenza.svg
    DESTINATION share/icons/hicolor/scalable/apps
    OPTIONAL
)

# The tools the app runs -- yt-dlp, which searches and downloads, and the
# quickjs it deciphers YouTube's stream URLs with -- travel next to the
# executable, in `tools/`, which is the first place it looks for them (ffmpeg is
# optional: without it the audio is saved in the container YouTube served).
# `packaging/tools/fetch-tools.ps1` stages the pinned Windows builds and
# `fetch-tools.sh` the Linux ones; a build that never ran either installs none
# of this and uses whatever PATH has.
#
# The staged names carry the platform's executable suffix, because that is what
# the app looks for: `yt-dlp.exe` on Windows, a bare `yt-dlp` everywhere else.
if(WIN32)
    set(CADENZA_TOOL_SUFFIX ".exe")
else()
    set(CADENZA_TOOL_SUFFIX "")
endif()

foreach(tool yt-dlp qjs ffmpeg)
    set(staged "${CMAKE_CURRENT_SOURCE_DIR}/packaging/tools/${tool}${CADENZA_TOOL_SUFFIX}")
    if(EXISTS "${staged}")
        install(PROGRAMS "${staged}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/tools"
        )
    endif()
endforeach()

# So a run out of the build tree finds them the way an installed one does:
# `tools/` beside the executable. Symlinks, because these are tens of megabytes
# of binaries that a rebuild has no reason to copy -- copied only where the
# filesystem will not take one, which is a Windows that has not been told to
# allow it.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
foreach(tool yt-dlp qjs ffmpeg)
    set(staged "${CMAKE_CURRENT_SOURCE_DIR}/packaging/tools/${tool}${CADENZA_TOOL_SUFFIX}")
    set(link "${CMAKE_BINARY_DIR}/tools/${tool}${CADENZA_TOOL_SUFFIX}")
    if(EXISTS "${staged}" AND NOT EXISTS "${link}")
        file(CREATE_LINK "${staged}" "${link}" SYMBOLIC COPY_ON_ERROR)
    endif()
endforeach()

include(CPack)
set(CPACK_PACKAGE_NAME "cadenza")
set(CPACK_PACKAGE_VENDOR "zxuru")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "zxuru")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
