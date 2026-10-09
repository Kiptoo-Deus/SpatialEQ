#!/bin/zsh
# Builds the DSP tests and the app (Release). Usage: scripts/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
CMAKE=${CMAKE:-/opt/homebrew/bin/cmake}
$CMAKE -S ../DSP -B build/dsp -DCMAKE_BUILD_TYPE=Release >/dev/null
$CMAKE --build build/dsp >/dev/null
./build/dsp/dsp_tests
xcodegen generate >/dev/null
xcodebuild -project SpatialEQ.xcodeproj -scheme SpatialEQ -configuration Release -destination 'generic/platform=macOS' \
  -derivedDataPath build/xcode build | grep -E "error:|BUILD" || true
echo "App: build/xcode/Build/Products/Release/SpatialEQ.app"
