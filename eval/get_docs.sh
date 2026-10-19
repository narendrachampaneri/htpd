#!/usr/bin/env bash
# Get the public PDFs of the evaluation and convert them into bundles.
#
#   eval/get_docs.sh                 download what is missing (needs the URLs in eval/docs.list)
#   eval/get_docs.sh --from DIR      take <name>.pdf from DIR instead of downloading
#
# Every PDF is checked against the SHA-256 in eval/docs.list, so every run and
# every agent tests on the same files. A line with "-" as SHA-256 is not pinned
# yet: the script prints the SHA-256 to write into eval/docs.list.
#
# Then each PDF is converted with build/htpd into eval/bundles/<name>.htpd
# (run `make` first) and the bundle is checked with the reader.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
from=""
if [[ "${1:-}" == "--from" ]]; then
  from="${2:?--from needs a folder}"
fi

mkdir -p "$here/docs" "$here/bundles"
status=0
while read -r name split sha url _; do
  [[ -z "$name" || "$name" == \#* ]] && continue
  pdf="$here/docs/$name.pdf"
  if [[ ! -f "$pdf" ]]; then
    if [[ -n "$from" && -f "$from/$name.pdf" ]]; then
      cp "$from/$name.pdf" "$pdf"
    elif [[ "$url" != "-" && -n "$url" ]]; then
      echo "download $name from $url"
      curl --fail --location --retry 3 --silent --show-error --output "$pdf.part" "$url"
      mv "$pdf.part" "$pdf"
    else
      echo "MISSING $name: put the PDF at eval/docs/$name.pdf or use --from DIR" >&2
      status=1
      continue
    fi
  fi
  got="$(sha256sum "$pdf" | cut -d' ' -f1)"
  if [[ "$sha" == "-" ]]; then
    echo "NOT PINNED $name: write $got into eval/docs.list"
  elif [[ "$got" != "$sha" ]]; then
    echo "WRONG FILE $name: SHA-256 is $got, eval/docs.list wants $sha" >&2
    status=1
    continue
  fi
  if [[ ! -x "$root/build/htpd" ]]; then
    echo "build/htpd is missing: run make first" >&2
    exit 1
  fi
  "$root/build/htpd" convert "$pdf" -o "$here/bundles/$name.htpd" --force >/dev/null
  (cd "$root" && PYTHONPATH=reader python3 -m htpd_reader check "eval/bundles/$name.htpd" --pdf "$pdf" | tail -1) || status=1
done < "$here/docs.list"
exit $status
