#!/usr/bin/env bash
# Downloads PDFium (the PDF engine of Chrome, already compiled) for HTPD.
#
#   ./scripts/get_pdfium.sh                           the version HTPD was tested with
#   PDFIUM_VERSION=latest ./scripts/get_pdfium.sh     the newest version
#   PDFIUM_VERSION=chromium/8100 ./scripts/get_pdfium.sh    any other version
#   ./scripts/get_pdfium.sh /some/folder              another target folder
#                                                     (default: third_party/pdfium)
#
# The files come from https://github.com/bblanchon/pdfium-binaries. That is a
# community project which compiles Google's PDFium; it is not Google itself.
# For the tested version the download is compared with a known SHA-256, so a
# changed or damaged file is refused. For any other version the SHA-256 is
# printed; to check it, pass the value you expect in PDFIUM_SHA256.
#
# Needs: bash, curl or wget, tar, sha256sum.
set -euo pipefail

tested_version="chromium/8086"   # PDFium 157.0.8086.0
declare -A tested_sha256=(
  [linux-x64]=588577cf52dabc1a444988bac841920df54cc2f141801424de97ab04f4fbb935
  [linux-arm64]=e7e2fe4686925618330103cb167950aca5a84bb00fd977a41b86be59dd1480a2
  [linux-arm]=47eb5b9a7ca0f97d372bd68fc87de3fb5d3e8984962275769c074ba1a93255a6
  [linux-x86]=58a3708ab3d3df2edc7e7f603294975501dffefe826272cbb84b91f934e3d7c1
  [linux-musl-x64]=82d96a7e15bfcee9a3ea886cefa0a6fac0b15b7b2e5e9575583a642803f28051
  [linux-musl-arm64]=ed147da758763ed3b8324de4041a8dc9e3e50a20f775ef52d970baabdcf8d74e
  [linux-musl-x86]=9ff44ab891e1ed9956d691f9796438c293645e644cb284c6ea2d4db7705fbc8b
)
releases="https://github.com/bblanchon/pdfium-binaries/releases"

die() {
  echo "get_pdfium.sh: $*" >&2
  exit 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'
  exit 0
fi
[[ $# -le 1 ]] || die "too many arguments (try --help)"

[[ "$(uname -s)" == "Linux" ]] || die "this script is for Linux. On other systems get PDFium from
$releases and pass its folder as PDFIUM_ROOT."

# Which build: ask the compiler what it builds for (a 32-bit system can run on
# a 64-bit kernel, so the kernel is only the second choice).
machine="$("${CXX:-g++}" -dumpmachine 2>/dev/null || true)"
[[ -n "$machine" ]] || machine="$(uname -m)"
case "$machine" in
  x86_64*|amd64*)  cpu=x64 ;;
  aarch64*|arm64*) cpu=arm64 ;;
  arm*)            cpu=arm ;;
  i?86*)           cpu=x86 ;;
  *) die "there is no ready-made PDFium for '$machine'.
Build PDFium yourself (https://pdfium.googlesource.com/pdfium) and pass its folder as PDFIUM_ROOT." ;;
esac
libc=""
[[ "$machine" == *musl* ]] && libc="musl-"
platform="linux-${libc}${cpu}"

version="${PDFIUM_VERSION:-$tested_version}"
if [[ "$version" == "latest" ]]; then
  url="$releases/latest/download/pdfium-$platform.tgz"
else
  url="$releases/download/${version//\//%2F}/pdfium-$platform.tgz"
fi

expected="${PDFIUM_SHA256:-}"
if [[ -z "$expected" && "$version" == "$tested_version" ]]; then
  expected="${tested_sha256[$platform]:-}"
fi

# Target folder. An existing folder is only replaced when it holds PDFium.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-$script_dir/../third_party/pdfium}"
mkdir -p "$(dirname "$target")"
target="$(cd "$(dirname "$target")" && pwd)/$(basename "$target")"
if [[ -e "$target" ]]; then
  [[ -d "$target" ]] || die "'$target' exists and is not a folder"
  if [[ -n "$(ls -A "$target")" && ! -f "$target/include/fpdfview.h" ]]; then
    die "'$target' is not empty and does not hold PDFium; it is not touched.
Remove it yourself or give another folder."
  fi
fi

# Download and unpack next to the target, then move into place in one step.
work="$(mktemp -d "$(dirname "$target")/.pdfium-download.XXXXXX")"
trap 'rm -rf "$work"' EXIT
archive="$work/pdfium.tgz"

echo "Downloading PDFium ($version, $platform)"
echo "  from $url"
if command -v curl >/dev/null 2>&1; then
  curl --fail --location --retry 3 --silent --show-error --output "$archive" "$url" ||
    die "download failed. Is there a build named 'pdfium-$platform.tgz' in version '$version'?
See $releases"
elif command -v wget >/dev/null 2>&1; then
  wget --quiet --tries=3 --output-document="$archive" "$url" ||
    die "download failed. Is there a build named 'pdfium-$platform.tgz' in version '$version'?
See $releases"
else
  die "curl or wget is needed (sudo apt install curl)"
fi

actual="$(sha256sum "$archive" | cut -d' ' -f1)"
if [[ -n "$expected" ]]; then
  [[ "$actual" == "$expected" ]] || die "the downloaded file is not the expected one.
  expected SHA-256: $expected
  downloaded:       $actual
Nothing was installed."
  echo "  SHA-256 is as expected: $actual"
else
  echo "  SHA-256 (not checked, no expected value known): $actual"
fi

mkdir "$work/pdfium"
tar -xzf "$archive" -C "$work/pdfium" --no-same-owner
[[ -f "$work/pdfium/include/fpdfview.h" && -f "$work/pdfium/lib/libpdfium.so" ]] ||
  die "the archive does not have include/fpdfview.h and lib/libpdfium.so"

rm -rf "$target"
mv "$work/pdfium" "$target"

if [[ -f "$target/VERSION" ]]; then
  number="$(sed -n 's/^[A-Z]*=//p' "$target/VERSION" | paste -sd. -)"
  echo "PDFium $number is ready in $target"
else
  echo "PDFium is ready in $target"
fi
echo "Its licences are in $target/licenses/ (PDFium itself: pdfium.txt)."
echo "Next step: make && make test"
