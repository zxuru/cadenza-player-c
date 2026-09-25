#!/usr/bin/env bash
#
# Builds a single-file AppImage of Cadenza.
#
# An AppImage is the closest thing Linux has to a portable executable: one
# file, no installation, no root. It bundles Qt, libmpv and TagLib, so it runs
# on a distribution that has none of them.
#
# Requirements: cmake, a C++20 compiler, the Qt6/libmpv/TagLib dev packages,
# and linuxdeploy + linuxdeploy-plugin-qt on PATH (or in the repo root).
#
# Usage: packaging/linux/build-appimage.sh [build-dir]

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${repo_root}/build}"
appdir="${build_dir}/AppDir"

for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "error: ${tool} not found on PATH" >&2
        echo "       fetch it from https://github.com/linuxdeploy/linuxdeploy/releases" >&2
        exit 1
    fi
done

echo "==> configuring"
cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo "==> building"
cmake --build "${build_dir}" --parallel "$(nproc)"

echo "==> staging into AppDir"
rm -rf "${appdir}"
DESTDIR="${appdir}" cmake --install "${build_dir}"

# The app looks for its helper tools in <exe dir>/tools/ before PATH, so drop
# whatever fetch-tools.sh staged next to the installed binary. linuxdeploy
# installs the executable into usr/bin/, hence usr/bin/tools/.
echo "==> bundling helper tools"
tools_src="${repo_root}/packaging/tools"
if [[ -d "${tools_src}" ]]; then
    mkdir -p "${appdir}/usr/bin/tools"
    bundled=0
    for tool in yt-dlp qjs ffmpeg; do
        if [[ -f "${tools_src}/${tool}" ]]; then
            install -m 0755 "${tools_src}/${tool}" "${appdir}/usr/bin/tools/${tool}"
            echo "    + ${tool}"
            bundled=1
        fi
    done
    if [[ "${bundled}" -eq 0 ]]; then
        echo "    (packaging/tools exists but has no tools staged yet; run fetch-tools.sh)"
    elif [[ ! -f "${tools_src}/ffmpeg" ]]; then
        echo "    note: no ffmpeg staged; downloads will land untagged, in the container YouTube served"
        echo "          (packaging/tools/fetch-tools.sh --ffmpeg stages a static one)"
    fi
else
    echo "    no ${tools_src} directory; shipping without bundled tools"
fi

# linuxdeploy derives the desktop file name and the icon name from these.
export LINUXDEPLOY_OUTPUT_VERSION="${LINUXDEPLOY_OUTPUT_VERSION:-1.0.0}"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export EXTRA_QT_MODULES="quick;qml;sql;concurrent;network"
export EXTRA_PLUGINS="sqldrivers;imageformats;iconengines;platforms;platformthemes;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"

echo "==> running linuxdeploy"
cd "${appdir}"
linuxdeploy \
    --appdir "${appdir}" \
    --plugin qt \
    --desktop-file "${appdir}/usr/share/applications/cadenza.desktop" \
    --icon-file "${appdir}/usr/share/icons/hicolor/scalable/apps/cadenza.svg" \
    --output appimage

mv ./*.AppImage "${repo_root}/dist/" 2>/dev/null || {
    mkdir -p "${repo_root}/dist"
    mv ./*.AppImage "${repo_root}/dist/"
}

echo "==> done: ${repo_root}/dist/"
ls -1 "${repo_root}/dist/"
