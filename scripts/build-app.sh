#!/bin/zsh
# Builds build/Garimpo.app (release) with the command-line tool inside it.
#   scripts/build-app.sh            build only
#   scripts/build-app.sh --install  also copy to /Applications
set -euo pipefail

cd "${0:A:h}/.."
VERSION="${VERSION:-1.0.0}"
BUILD_NUMBER="${BUILD_NUMBER:-$(date +%Y%m%d%H%M)}"
APP="build/Garimpo.app"

swift build -c release --product Garimpo
swift build -c release --product garimpo-cli
BIN="$(swift build -c release --show-bin-path)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Helpers"
cp "$BIN/Garimpo" "$APP/Contents/MacOS/Garimpo"
cp "$BIN/garimpo-cli" "$APP/Contents/Helpers/garimpo"
cp Resources/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
sed -e "s/__VERSION__/$VERSION/" -e "s/__BUILD__/$BUILD_NUMBER/" Resources/Info.plist > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

codesign --force --deep --sign - "$APP"
echo "Built $APP ($VERSION, build $BUILD_NUMBER)"

if [[ "${1:-}" == "--install" ]]; then
    rm -rf /Applications/Garimpo.app
    cp -R "$APP" /Applications/
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Garimpo.app
    echo "Installed /Applications/Garimpo.app"
fi
