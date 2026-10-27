#!/bin/zsh
# The macOS release: builds the app and the plug-ins, then writes build/release-macos:
#   SpatialEQ-<version>-macOS.pkg — the main installer: app + VST3 + Audio Unit (each selectable)
#   SpatialEQ-<version>.dmg       — the app alone, drag to Applications
# Uses macos/scripts/package.sh and plugin/scripts/package_mac.sh; the same environment variables apply
# (CMAKE, CMAKE_ARGS, DEVELOPER_ID, INSTALLER_ID).
set -euo pipefail
cd "$(dirname "$0")/../.."
macos/scripts/package.sh
plugin/scripts/package_mac.sh

VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" plugin/resource/Info.plist)
OUT=build/release-macos
RES=build/release-macos-resources
rm -rf "$OUT" "$RES" && mkdir -p "$OUT" "$RES"
cp branding/installer/welcome.html "$RES/"
tiffutil -cathidpicheck branding/installer/pkg-background.png branding/installer/pkg-background@2x.png \
  -out "$RES/pkg-background.tiff" 2>/dev/null
sed "s/VERSION/$VERSION/" branding/installer/distribution-macos.xml > build/distribution-macos.xml
# Component packages: the app's from package.sh, the plug-ins' from package_mac.sh.
productbuild --distribution build/distribution-macos.xml --resources "$RES" \
  --package-path macos/build --package-path build/plugin-pkg \
  ${INSTALLER_ID:+--sign "$INSTALLER_ID"} "$OUT/SpatialEQ-$VERSION-macOS.pkg"
cp "macos/build/release/SpatialEQ-$VERSION.dmg" "$OUT/"

(cd "$OUT" && shasum -a 256 *.pkg *.dmg > SHA256SUMS-macOS.txt)
ls -lh "$OUT"
