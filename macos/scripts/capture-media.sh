#!/bin/zsh
# Regenerates the README screenshots and the 360° orbit video using the app's --capture mode.
# Plays a synthesized demo track (scripts/make_demo_music.py) through the system while recording.
set -euo pipefail
cd "$(dirname "$0")/.."
APP=build/xcode/Build/Products/Release/SpatialEQ.app
OUT=$PWD/build/media
TMP=$(mktemp -d)
rm -rf "$OUT" && mkdir -p "$OUT"
python3 scripts/make_demo_music.py "$TMP/demo.wav"
open -n "$APP" --args --capture "$OUT"
for i in {1..240}; do [[ -f $OUT/ready ]] && break; sleep 1; done
afplay "$TMP/demo.wav" & PLAY=$!
while pgrep -f "$APP/Contents/MacOS" >/dev/null; do sleep 1; done
kill $PLAY 2>/dev/null || true
mkdir -p ../docs
cp "$OUT/idle.png" ../docs/screenshot-idle.png
cp "$OUT/playing.png" ../docs/screenshot-playing.png
cp "$OUT/orbit.mp4" ../docs/spatialeq-360.mp4
ffmpeg -v error -y -i "$OUT/orbit.mp4" -vf "fps=12,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=sierra2_4a" ../docs/spatialeq-360.gif
ls -lh ../docs
