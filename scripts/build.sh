#!/bin/sh
# Builds Garimpo, runs the tests and optionally installs it.
#
#   scripts/build.sh                build into build/ and run the tests
#   scripts/build.sh --install      macOS: copy Garimpo.app to /Applications
#                                   Linux: install into ~/.local (bin, desktop entry, icon)
#   scripts/build.sh --standalone   macOS: bundle the Qt libraries inside the app (macdeployqt)
#   scripts/build.sh --package      macOS: standalone build zipped for a release in build/
set -eu

cd "$(dirname "$0")/.."
INSTALL=0
STANDALONE=0
PACKAGE=0
for arg in "$@"; do
    case "$arg" in
        --install) INSTALL=1 ;;
        --standalone) STANDALONE=1 ;;
        --package) STANDALONE=1; PACKAGE=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

PREFIX_PATH=""
if [ "$(uname)" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
    PREFIX_PATH="$(brew --prefix qtbase 2>/dev/null || true)"
fi

GENERATOR=""
command -v ninja >/dev/null 2>&1 && GENERATOR="-G Ninja"

# shellcheck disable=SC2086
cmake -S . -B build $GENERATOR -DCMAKE_BUILD_TYPE=Release ${PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$PREFIX_PATH"}
cmake --build build --parallel
ctest --test-dir build --output-on-failure

if [ "$(uname)" = "Darwin" ]; then
    APP=build/Garimpo.app
    if [ "$STANDALONE" = 1 ]; then
        # Drop libraries left over from an earlier run so only current ones ship.
        rm -rf "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" "$APP/Contents/Resources/qt.conf"
        cmake --build build --target garimpo-app
        "$PREFIX_PATH/bin/macdeployqt" "$APP" -always-overwrite \
            -executable="$APP/Contents/Helpers/garimpo"
    fi
    codesign --force --deep --sign - "$APP"
    echo "Built $APP"
    if [ "$PACKAGE" = 1 ]; then
        VERSION="$(plutil -extract CFBundleShortVersionString raw "$APP/Contents/Info.plist")"
        ZIP="build/Garimpo-$VERSION-macos-$(uname -m).zip"
        rm -f "$ZIP"
        ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
        echo "Packaged $ZIP"
        shasum -a 256 "$ZIP"
    fi
    if [ "$INSTALL" = 1 ]; then
        rm -rf /Applications/Garimpo.app
        cp -R "$APP" /Applications/
        echo "Installed /Applications/Garimpo.app"
    fi
else
    echo "Built build/garimpo-gui and build/garimpo"
    if [ "$INSTALL" = 1 ]; then
        cmake --install build --prefix "$HOME/.local"
        command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" || true
        echo "Installed into $HOME/.local"
    fi
fi
