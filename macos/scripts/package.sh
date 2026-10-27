#!/bin/zsh
# Builds SpatialEQ and produces the installers in build/release:
#   SpatialEQ-<version>.pkg  — macOS Installer package, installs to /Applications
#   SpatialEQ-<version>.dmg  — drag-to-Applications disk image
# Both use the box artwork and icon from branding/. The disk image is laid out with dmgbuild
# (installed into build/venv on first use; needs python3).
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
# Product archive with the welcome page and box art (branding/installer).
RES=build/pkgresources
rm -rf "$RES" && mkdir -p "$RES"
cp ../branding/installer/welcome.html "$RES/"
tiffutil -cathidpicheck ../branding/installer/pkg-background.png ../branding/installer/pkg-background@2x.png \
  -out "$RES/pkg-background.tiff" 2>/dev/null # one file with both resolutions
sed "s/VERSION/$VERSION/" ../branding/installer/distribution-app.xml > build/distribution.xml
productbuild --distribution build/distribution.xml --resources "$RES" --package-path build \
  ${INSTALLER_ID:+--sign "$INSTALLER_ID"} "$OUT/SpatialEQ-$VERSION.pkg"

# Disk image: the app, an Applications shortcut and the box art background.
if [[ ! -x build/venv/bin/dmgbuild ]]; then
  python3 -m venv build/venv
  build/venv/bin/pip install --quiet "dmgbuild==1.6.5"
fi
build/venv/bin/dmgbuild -s ../branding/dmg-settings.py -D app="$APP" -D branding="$(cd ../branding && pwd)" "SpatialEQ $VERSION" "$OUT/SpatialEQ-$VERSION.dmg" >/dev/null

(cd "$OUT" && shasum -a 256 *.pkg *.dmg > SHA256SUMS.txt)
ls -lh "$OUT"
