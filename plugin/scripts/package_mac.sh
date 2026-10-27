#!/bin/zsh
# Builds the SpatialEQ plug-ins for macOS (universal) and the installer in build/release-plugin:
#   SpatialEQ-Plugins-<version>-macOS.pkg — VST3 to /Library/Audio/Plug-Ins/VST3 and the Audio Unit
#   to /Library/Audio/Plug-Ins/Components (each one can be deselected in the installer).
# Needs Xcode, CMake 3.25+ and Node.js 20+. Set DEVELOPER_ID / INSTALLER_ID to sign with your
# certificates; otherwise everything is ad-hoc signed. Extra CMake arguments can be passed in
# CMAKE_ARGS (e.g. -DFETCHCONTENT_SOURCE_DIR_VST3SDK=...).
set -euo pipefail
cd "$(dirname "$0")/../.."
CMAKE=${CMAKE:-cmake}
BUILD=build/plugin-xcode
OUT=build/release-plugin

(cd plugin/ui && { [[ -d node_modules ]] || npm ci; } && npm run build >/dev/null)
$CMAKE -S plugin -B $BUILD -G Xcode ${=CMAKE_ARGS:-} \
  ${DEVELOPER_ID:+-DSMTG_CODE_SIGN_IDENTITY_MAC="$DEVELOPER_ID"} >/dev/null
# Builds the VST3 (and runs the Steinberg validator), then the AU that wraps it.
$CMAKE --build $BUILD --config Release --target SpatialEQ_AU -- -quiet

PRODUCTS=$BUILD/VST3/Release
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" plugin/resource/Info.plist)
STAGE=build/plugin-pkg
rm -rf "$OUT" "$STAGE" && mkdir -p "$OUT" "$STAGE/vst3" "$STAGE/au" "$STAGE/resources"

ditto "$PRODUCTS/SpatialEQ.vst3" "$STAGE/vst3/SpatialEQ.vst3"
# The AU loads the VST3 from Contents/Resources/plugin.vst3, which the build links to the build folder;
# ship a real copy instead.
ditto "$PRODUCTS/SpatialEQ.component" "$STAGE/au/SpatialEQ.component"
rm -rf "$STAGE/au/SpatialEQ.component/Contents/Resources/plugin.vst3"
ditto "$PRODUCTS/SpatialEQ.vst3" "$STAGE/au/SpatialEQ.component/Contents/Resources/plugin.vst3"

SIGN_ID=${DEVELOPER_ID:--}
for bundle in "$STAGE/vst3/SpatialEQ.vst3" "$STAGE/au/SpatialEQ.component/Contents/Resources/plugin.vst3" \
              "$STAGE/au/SpatialEQ.component"; do
  codesign --force --options runtime ${DEVELOPER_ID:+--timestamp} --sign "$SIGN_ID" "$bundle"
done

component_pkg() { # <stage dir> <identifier> <install location> <output>
  pkgbuild --analyze --root "$1" "$STAGE/component.plist" >/dev/null
  # Install to the plug-in folder even if the user has a copy elsewhere (plug-ins get no key by default).
  /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$STAGE/component.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :0:BundleIsRelocatable bool false" "$STAGE/component.plist"
  pkgbuild --root "$1" --component-plist "$STAGE/component.plist" --identifier "$2" --version "$VERSION" \
    --install-location "$3" "$4" >/dev/null
}
component_pkg "$STAGE/vst3" com.savannahdsp.spatialeq.vst3 /Library/Audio/Plug-Ins/VST3 "$STAGE/SpatialEQ-vst3.pkg"
component_pkg "$STAGE/au" com.savannahdsp.spatialeq.au /Library/Audio/Plug-Ins/Components "$STAGE/SpatialEQ-au.pkg"

# Product archive with the welcome page and box art (branding/installer).
cp branding/installer/welcome.html "$STAGE/resources/"
tiffutil -cathidpicheck branding/installer/pkg-background.png branding/installer/pkg-background@2x.png \
  -out "$STAGE/resources/pkg-background.tiff" 2>/dev/null
sed "s/VERSION/$VERSION/" branding/installer/distribution-plugins.xml > "$STAGE/distribution.xml"
productbuild --distribution "$STAGE/distribution.xml" --resources "$STAGE/resources" --package-path "$STAGE" \
  ${INSTALLER_ID:+--sign "$INSTALLER_ID"} "$OUT/SpatialEQ-Plugins-$VERSION-macOS.pkg"

(cd "$OUT" && shasum -a 256 *.pkg > SHA256SUMS.txt)
ls -lh "$OUT"
