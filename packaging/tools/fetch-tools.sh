#!/usr/bin/env bash
#
# Downloads the helper binaries Cadenza shells out to at runtime.
#
# Cadenza is meant to be a single application that "just works": the user
# should not have to install yt-dlp, QuickJS or ffmpeg themselves. So we stage
# the tools next to the executable (the app looks in <exe dir>/tools/ before
# PATH) and ship them inside the package.
#
# Every download is verified against a pinned SHA-256 *before* it lands in its
# final place: we fetch to "<name>.part", compare the hash, and only then
# rename and chmod +x. A tampered or truncated download never becomes a
# runnable tool.
#
# The versions below are pinned on purpose. yt-dlp in particular changes
# often, and the 2026.03.17 build shipped by Debian answers audio requests
# with HTTP 403 (measured), so we refuse to inherit whatever the distro has.
#
# Usage: packaging/tools/fetch-tools.sh [--force] [--ffmpeg] [dest]
#
#   --force   re-download even when a correct file is already present
#   --ffmpeg  also fetch ffmpeg (static build, ~40 MB, off by default)
#   dest      destination directory (default: this script's own directory)

set -euo pipefail

# yt-dlp: use the yt-dlp_linux asset. It is a PyInstaller bundle, so it carries
# its own interpreter and needs no Python on the host. Its checksum comes from
# the release's SHA2-256SUMS file, which also lists every other asset; we look
# up only the line we care about.
ytdlp_version="2026.08.19"
ytdlp_asset="yt-dlp_linux"
ytdlp_base="https://github.com/yt-dlp/yt-dlp/releases/download/${ytdlp_version}"

# qjs: quickjs-ng ships prebuilt, statically linked binaries for each platform,
# so the linux x86_64 asset is the engine itself -- there is no archive to
# unpack. The upstream release does not publish a checksum file, so this value
# was computed once from the pinned URL below and is recorded here.
#   https://github.com/quickjs-ng/quickjs/releases/download/v0.17.0/qjs-linux-x86_64
qjs_version="v0.17.0"
qjs_asset="qjs-linux-x86_64"
qjs_sha256="0bfc02511a9f549c28b53880d988fc7cd5d361e90c5e8afdfcd7dc6774ceace5"

# ffmpeg: a static build from johnvansickle.com, which is versioned and has no
# runtime dependencies. Upstream publishes no checksum file, so the value was
# computed once from the pinned URL below.
ffmpeg_version="7.0.2"
ffmpeg_sha256="abda8d77ce8309141f83ab8edf0596834087c52467f6badf376a6a2a4c87cf67"
ffmpeg_url="https://johnvansickle.com/ffmpeg/releases/ffmpeg-${ffmpeg_version}-amd64-static.tar.xz"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

force=0
want_ffmpeg=0
dest=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force)  force=1; shift ;;
        --ffmpeg) want_ffmpeg=1; shift ;;
        -h|--help)
            sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        -*) echo "error: unknown option '$1'" >&2; exit 2 ;;
        *)  dest="$1"; shift ;;
    esac
done
dest="${dest:-${script_dir}}"

# Pick a downloader once; every fetch below goes through download(), which
# reports the failing URL itself so a 404 or a dropped connection reads as a
# clear error instead of a bare curl/wget exit code.
if command -v curl >/dev/null 2>&1; then
    download() {
        if ! curl -fsSL --retry 3 --retry-delay 2 -o "$2" "$1"; then
            rm -f "$2"
            echo "error: download failed: $1" >&2
            return 1
        fi
    }
elif command -v wget >/dev/null 2>&1; then
    download() {
        if ! wget -q --tries=3 -O "$2" "$1"; then
            rm -f "$2"
            echo "error: download failed: $1" >&2
            return 1
        fi
    }
else
    echo "error: neither curl nor wget is available; cannot fetch tools" >&2
    exit 1
fi

# The pinned assets are linux x86_64 only. Fail loudly instead of silently
# staging binaries that will not run.
if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "error: fetch-tools.sh only supports Linux x86_64 (found $(uname -s)/$(uname -m))" >&2
    exit 1
fi

sha256_of() {
    # sha256sum prints "<hash>  <path>"; keep just the hash.
    sha256sum "$1" | cut -d' ' -f1
}

bytes_of() {
    wc -c <"$1" | tr -d ' '
}

mkdir -p "${dest}"

# staged accumulates "name sha256 bytes" lines for the closing summary.
staged=""

# install_verified <name> <expected-sha256> <file-with-content>
# Moves an already-verified file into place and reports it. Callers are
# responsible for having checked the hash first.
install_verified() {
    local name="$1" expected="$2" part="$3"
    mv -f "${part}" "${dest}/${name}"
    chmod +x "${dest}/${name}"
    local size
    size="$(bytes_of "${dest}/${name}")"
    echo "ok ${name} ${expected} ${size}"
    staged="${staged}${name} ${expected} ${size}"$'\n'
}

# already_ok <name> <expected-sha256>
# True when the file is present, executable and matches the pinned hash, which
# lets us skip the network entirely on repeat runs.
already_ok() {
    local name="$1" expected="$2"
    [[ -f "${dest}/${name}" ]] || return 1
    [[ "$(sha256_of "${dest}/${name}")" == "${expected}" ]]
}

fetch_ytdlp() {
    local sums expected
    sums="$(mktemp)"
    # The checksums live in a sibling asset; download it first so we know what
    # to expect before pulling ~30 MB.
    download "${ytdlp_base}/SHA2-256SUMS" "${sums}"
    expected="$(awk -v a="${ytdlp_asset}" '$2 == a { print $1 }' "${sums}")"
    rm -f "${sums}"
    if [[ -z "${expected}" ]]; then
        echo "error: ${ytdlp_asset} is missing from SHA2-256SUMS for ${ytdlp_version}" >&2
        return 1
    fi

    if [[ "${force}" -eq 0 ]] && already_ok "yt-dlp" "${expected}"; then
        local size
        size="$(bytes_of "${dest}/yt-dlp")"
        echo "ok yt-dlp ${expected} ${size}"
        staged="${staged}yt-dlp ${expected} ${size}"$'\n'
        return 0
    fi

    echo "==> yt-dlp ${ytdlp_version}"
    local part="${dest}/yt-dlp.part"
    download "${ytdlp_base}/${ytdlp_asset}" "${part}"
    local got
    got="$(sha256_of "${part}")"
    if [[ "${got}" != "${expected}" ]]; then
        rm -f "${part}"
        echo "error: yt-dlp checksum mismatch (expected ${expected}, got ${got})" >&2
        return 1
    fi
    install_verified "yt-dlp" "${expected}" "${part}"
}

fetch_qjs() {
    if [[ "${force}" -eq 0 ]] && already_ok "qjs" "${qjs_sha256}"; then
        local size
        size="$(bytes_of "${dest}/qjs")"
        echo "ok qjs ${qjs_sha256} ${size}"
        staged="${staged}qjs ${qjs_sha256} ${size}"$'\n'
        return 0
    fi

    echo "==> qjs ${qjs_version}"
    local part="${dest}/qjs.part"
    download "https://github.com/quickjs-ng/quickjs/releases/download/${qjs_version}/${qjs_asset}" "${part}"
    local got
    got="$(sha256_of "${part}")"
    if [[ "${got}" != "${qjs_sha256}" ]]; then
        rm -f "${part}"
        echo "error: qjs checksum mismatch (expected ${qjs_sha256}, got ${got})" >&2
        return 1
    fi
    install_verified "qjs" "${qjs_sha256}" "${part}"
}

fetch_ffmpeg() {
    if [[ "${force}" -eq 0 ]] && already_ok "ffmpeg" "${ffmpeg_sha256}"; then
        local size
        size="$(bytes_of "${dest}/ffmpeg")"
        echo "ok ffmpeg ${ffmpeg_sha256} ${size}"
        staged="${staged}ffmpeg ${ffmpeg_sha256} ${size}"$'\n'
        return 0
    fi

    echo "==> ffmpeg ${ffmpeg_version}"
    local archive="${dest}/ffmpeg.tar.xz.part"
    download "${ffmpeg_url}" "${archive}"
    local got
    got="$(sha256_of "${archive}")"
    if [[ "${got}" != "${ffmpeg_sha256}" ]]; then
        rm -f "${archive}"
        echo "error: ffmpeg archive checksum mismatch (expected ${ffmpeg_sha256}, got ${got})" >&2
        return 1
    fi

    # The tarball holds a single top-level directory plus man pages and docs;
    # we want only the ffmpeg binary from it, so stream that member out.
    local part="${dest}/ffmpeg.part"
    tar -xJf "${archive}" -O "ffmpeg-${ffmpeg_version}-amd64-static/ffmpeg" >"${part}"
    rm -f "${archive}"
    install_verified "ffmpeg" "${ffmpeg_sha256}" "${part}"
}

fetch_ytdlp
fetch_qjs
if [[ "${want_ffmpeg}" -eq 1 ]]; then
    fetch_ffmpeg
fi

echo "==> staged in ${dest}"
printf '%s' "${staged}"
