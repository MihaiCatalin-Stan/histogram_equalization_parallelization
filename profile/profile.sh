#!/usr/bin/env bash
# Runs the PROFILE build N times on an image and prints median per-step timings.
# Usage: [FORMAT=p5|p2] profile/profile.sh <image.(jpg|png|pgm)> [runs=20] [binary=<repo>/histogram_profile]
# FORMAT selects the PGM variant the input is converted to (default p5, binary;
# p2 is ASCII, which is what pipeline.sh currently produces). Ignored for .pgm input.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <image> [runs] [binary]"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

input="$1"
runs="${2:-20}"
bin_arg="${3:-$script_dir/../histogram_profile}"
bin="$(cd "$(dirname "$bin_arg")" && pwd)/$(basename "$bin_arg")"

[[ -f "$input" ]] || { echo "Error: input not found: $input"; exit 1; }
[[ -x "$bin" ]] || { echo "Error: $bin not found, run 'make profile'"; exit 1; }

# Work in a temp dir so *.pgm and *_output.pgm never land in the repo.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

name=$(basename -- "$input")
name="${name%.*}"

if [[ "${input##*.}" == "pgm" ]]; then
    cp "$input" "$tmp/$name.pgm"
else
    case "${FORMAT:-p5}" in
        p5) magick "$input" -colorspace Gray -define pgm:format=raw "$tmp/$name.pgm" ;;
        p2) magick "$input" -colorspace Gray -compress None "$tmp/$name.pgm" ;;
        *)  echo "Error: FORMAT must be p5 or p2"; exit 1 ;;
    esac
fi

cd "$tmp"
for ((i = 0; i < runs; i++)); do
    "$bin" "$name.pgm" 2>&1 >/dev/null | grep '^PROFILE'
done > runs.txt

python3 -I "$script_dir/summarize.py" "$name" "$runs" runs.txt
