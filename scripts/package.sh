#!/bin/zsh
# Builds SpatialEQ and produces the installers in build/release:
#   SpatialEQ-<version>.pkg  — macOS Installer package, installs to /Applications
#   SpatialEQ-<version>.dmg  — drag-to-Applications disk image
# Set DEVELOPER_ID / INSTALLER_ID to sign with your certificates; otherwise the app is ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh

APP=build/xcode/Build/Products/Release/SpatialEQ.app
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
OUT=build/release
STAGE=build/pkgroot
rm -rf "$OUT" "$STAGE" && mkdir -p "$OUT" "$STAGE"

if [[ -n "${DEVELOPER_ID:-}" ]]; then
  codesign --force --deep --options runtime --timestamp \
    --entitlements App/Support/SpatialEQ.entitlements --sign "$DEVELOPER_ID" "$APP"
fi
ditto "$APP" "$STAGE/SpatialEQ.app"

# Installer package. Not relocatable, so it always installs to /Applications.
pkgbuild --analyze --root "$STAGE" build/component.plist >/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" build/component.plist
pkgbuild --root "$STAGE" --component-plist build/component.plist \
  --identifier com.savannahdsp.spatialeq --version "$VERSION" \
  --install-location /Applications build/SpatialEQ-component.pkg
productbuild --package build/SpatialEQ-component.pkg \
  ${INSTALLER_ID:+--sign "$INSTALLER_ID"} "$OUT/SpatialEQ-$VERSION.pkg"

# Disk image with an Applications shortcut.
DMGROOT=build/dmgroot
rm -rf "$DMGROOT" && mkdir -p "$DMGROOT"
ditto "$APP" "$DMGROOT/SpatialEQ.app"
ln -s /Applications "$DMGROOT/Applications"
hdiutil create -volname "SpatialEQ $VERSION" -srcfolder "$DMGROOT" -ov -format UDZO "$OUT/SpatialEQ-$VERSION.dmg" >/dev/null

(cd "$OUT" && shasum -a 256 *.pkg *.dmg > SHA256SUMS.txt)
ls -lh "$OUT"
