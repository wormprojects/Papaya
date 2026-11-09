#!/bin/sh
# Regenerate include/logo.h from meta/papaya.aseprite.
#
#   tools/logo/mklogo.sh [file.aseprite]
#
# Needs the Aseprite app (for its command line) and python3 with Pillow.
# The .aseprite must have two layers: the word (bottom) and the note (top,
# drawn over the word and bobbing a bit more on the splash). They're
# picked by name, set WORD_LAYER / NOTE_LAYER if they get renamed.
# An optional third layer (OVERLAY_LAYER, "Layer 4") sits over the word at
# partial opacity and is flattened onto it, so it moves with the word.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
src=${1:-$root/meta/papaya.aseprite}
word=${WORD_LAYER:-Layer 2}
note=${NOTE_LAYER:-Layer 3}
overlay=${OVERLAY_LAYER:-Layer 4}
aseprite=${ASEPRITE:-/Applications/Aseprite.app/Contents/MacOS/aseprite}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$aseprite" -b --split-layers "$src" --save-as "$tmp/{layer}.png"
extra=
[ -e "$tmp/$overlay.png" ] && extra="$tmp/$overlay.png"
python3 "$here/mklogo.py" "$tmp/$word.png" "$tmp/$note.png" \
	"$root/include/logo.h" ${extra:+"$extra"}

# The HOME Menu banner: the whole logo (word, overlay, note) on a
# transparent 256x128 canvas, scaled down to fit (the one place the art
# isn't 1:1).
python3 "$here/mkbanner.py" "$tmp/$word.png" "$tmp/$note.png" \
	"$root/meta/banner.png" ${extra:+"$extra"}
