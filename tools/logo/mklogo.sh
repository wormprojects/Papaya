#!/bin/sh
# Regenerate include/logo.h from meta/papaya.aseprite.
#
#   tools/logo/mklogo.sh [file.aseprite]
#
# Needs the Aseprite app (for its command line) and python3 with Pillow.
# The .aseprite must have two layers: the word (bottom) and the note (top,
# drawn over the word and bobbing a bit more on the splash). They're
# picked by name, set WORD_LAYER / NOTE_LAYER if they get renamed.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
src=${1:-$root/meta/papaya.aseprite}
word=${WORD_LAYER:-Layer 2}
note=${NOTE_LAYER:-Layer 3}
aseprite=${ASEPRITE:-/Applications/Aseprite.app/Contents/MacOS/aseprite}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$aseprite" -b --split-layers "$src" --save-as "$tmp/{layer}.png"
python3 "$here/mklogo.py" "$tmp/$word.png" "$tmp/$note.png" \
	"$root/include/logo.h"
