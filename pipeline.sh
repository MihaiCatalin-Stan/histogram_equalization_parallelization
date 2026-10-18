#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <input-image> <C-program>"
    exit 1
fi

input="$1"
program="$2"

if [[ ! -f "$input" ]]; then
    echo "Error: Input file not found: $input"
    exit 1
fi

filename=$(basename -- "$input")
extension="${filename##*.}"
name="${filename%.*}"
extension_lower=$(printf '%s' "$extension" | tr '[:upper:]' '[:lower:]')

case "$extension_lower" in
    png|jpg|jpeg) ;;
    *)
        echo "Error: Only PNG, JPG, and JPEG are supported."
        exit 1
        ;;
esac

# Intermediate PGM files in the current directory
input_pgm="${name}.pgm"
output_pgm="${name}_output.pgm"
output="${name}_output.${extension}"

# Convert input image to grayscale binary PGM (P5)
convert "$input" -colorspace Gray \
    -compress None -define pgm:format=raw "$input_pgm"

# Your C program takes only the input filename
./"$program" "$input_pgm"

# Check that the expected output exists
if [[ ! -f "$output_pgm" ]]; then
    echo "Error: Expected output file was not created: $output_pgm"
    exit 1
fi

# Convert processed PGM back to the original image format
convert "$output_pgm" "$output"

echo "Done: $output"
