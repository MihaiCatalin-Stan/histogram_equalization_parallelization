#!/usr/bin/env bash
# Runs the PROFILE build N times on an image and prints median per-step timings.
# Usage: [FORMAT=p5|p2] ./profile.sh <image.(jpg|png|pgm)> [runs=20] [binary=./histogram_profile]
# FORMAT selects the PGM variant the input is converted to (default p5, binary;
# p2 is ASCII, which is what pipeline.sh currently produces). Ignored for .pgm input.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <image> [runs] [binary]"
    exit 1
fi

input="$1"
runs="${2:-20}"
bin="$(cd "$(dirname "${3:-./histogram_profile}")" && pwd)/$(basename "${3:-./histogram_profile}")"

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

python3 -I - "$name" "$runs" <<'PY'
import statistics, sys
rows = [l.split()[1:] for l in open("runs.txt")]
w, h = rows[0][0], rows[0][1]
cols = list(zip(*[[float(x) for x in r[2:]] for r in rows]))
labels = ["read", "alloc output", "1 histogram", "2+3 cdf/cdf_min",
          "4 mapping", "5 apply mapping", "write (P2)"]
med = [statistics.median(c) for c in cols]
mn = [min(c) for c in cols]
total = sum(med)
print(f"{sys.argv[1]}: {w}x{h} ({int(w)*int(h)/1e6:.1f} MP), median of {sys.argv[2]} runs")
print(f"{'step':<18}{'median ms':>12}{'min ms':>12}{'% total':>10}")
for l, m, n in zip(labels, med, mn):
    print(f"{l:<18}{m:>12.5f}{n:>12.5f}{100*m/total:>9.1f}%")
print(f"{'total':<18}{total:>12.5f}")
PY
