#!/usr/bin/env bash
# Convert a PDF into an HTPD file with the HTPD converter.
#
#   convert.sh DATA_DIR file.pdf [converter options]     e.g. --single, -o out.htpd
#
# The result goes next to the PDF (file.htpd) unless -o says otherwise.
#
# The converter is used from, in this order: $HTPD_CONVERTER, `htpd` on the
# PATH, DATA_DIR/htpd-<version>-linux-x64/htpd. When none is there, the
# release for Linux x64 is downloaded once from GitHub into DATA_DIR and its
# SHA-256 is checked. Nothing else is downloaded, and the PDF never leaves
# this computer.
set -euo pipefail

VERSION="0.1.2"
SHA256="fcfc2fadef4343b4be5e16e47381fe1f8016dc16b1343436df92627e90271791"
NAME="htpd-${VERSION}-linux-x64"
URL="https://github.com/narendrachampaneri/htpd/releases/download/v${VERSION}/${NAME}.tar.gz"

if [ $# -lt 2 ]; then
    echo "usage: convert.sh DATA_DIR file.pdf [converter options]" >&2
    exit 2
fi
data="$1"; shift
# an unreplaced ${CLAUDE_PLUGIN_DATA} (or an empty value) falls back to a folder of the user
case "$data" in ""|*'${'*) data="${XDG_DATA_HOME:-$HOME/.local/share}/htpd" ;; esac

find_converter() {
    if [ -n "${HTPD_CONVERTER:-}" ] && [ -x "$HTPD_CONVERTER" ]; then echo "$HTPD_CONVERTER"; return; fi
    if command -v htpd >/dev/null 2>&1; then command -v htpd; return; fi
    if [ -x "$data/$NAME/htpd" ]; then echo "$data/$NAME/htpd"; return; fi
}

conv="$(find_converter || true)"
if [ -z "$conv" ]; then
    if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
        echo "htpd: the ready converter is for Linux x64 (and WSL); build it from source on this system:" >&2
        echo "      https://github.com/narendrachampaneri/htpd#build-from-source" >&2
        exit 3
    fi
    mkdir -p "$data"
    tmp="$(mktemp -d "$data/.download.XXXXXX")"
    trap 'rm -rf "$tmp"' EXIT
    echo "htpd: downloading the converter $VERSION (about 4 MB) into $data" >&2
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$URL" -o "$tmp/htpd.tar.gz"
    else
        wget -q "$URL" -O "$tmp/htpd.tar.gz"
    fi
    echo "$SHA256  $tmp/htpd.tar.gz" | sha256sum -c --quiet - || { echo "htpd: checksum does not match" >&2; exit 4; }
    tar xzf "$tmp/htpd.tar.gz" -C "$tmp"
    rm -rf "${data:?}/$NAME"
    mv "$tmp/$NAME" "$data/$NAME"
    rm -rf "$tmp"
    trap - EXIT
    conv="$data/$NAME/htpd"
fi

# Without -o the result goes next to the PDF (the converter alone writes it in the current folder).
pdf="$1"
for a in "$@"; do
    case "$a" in -o|--output|--output=*) exec "$conv" convert "$@" ;; esac
done
base="$(basename "$pdf")"
exec "$conv" convert "$@" -o "$(dirname "$pdf")/${base%.*}.htpd"
