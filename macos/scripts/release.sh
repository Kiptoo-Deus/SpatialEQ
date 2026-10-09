#!/bin/zsh
# Signs with Developer ID, notarizes, staples and packages a DMG.
#   DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE=spatialeq-notary scripts/release.sh
# Create the notary profile once with:
#   xcrun notarytool store-credentials spatialeq-notary --apple-id you@example.com --team-id TEAMID
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVELOPER_ID:?set DEVELOPER_ID}" "${NOTARY_PROFILE:?set NOTARY_PROFILE}"
scripts/build.sh
APP=build/xcode/Build/Products/Release/SpatialEQ.app
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
codesign --force --deep --options runtime --timestamp \
  --entitlements App/Support/SpatialEQ.entitlements --sign "$DEVELOPER_ID" "$APP"
codesign --verify --strict --verbose=2 "$APP"
mkdir -p build/release
DMG=build/release/SpatialEQ-$VERSION.dmg
rm -f "$DMG"
hdiutil create -volname SpatialEQ -srcfolder "$APP" -ov -format UDZO "$DMG"
codesign --sign "$DEVELOPER_ID" --timestamp "$DMG"
xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$DMG"
echo "Release: $DMG"
