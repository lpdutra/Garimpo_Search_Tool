#!/bin/zsh
# Runs the test suite. With only the Command Line Tools installed (no Xcode),
# the Swift Testing framework lives outside the default search paths.
set -euo pipefail
cd "${0:A:h}/.."

CLT=/Library/Developer/CommandLineTools/Library/Developer
if [[ -d "$CLT/Frameworks/Testing.framework" ]] && ! xcode-select -p | grep -q Xcode.app; then
    exec swift test \
        -Xswiftc -F -Xswiftc "$CLT/Frameworks" \
        -Xlinker -F -Xlinker "$CLT/Frameworks" \
        -Xlinker -rpath -Xlinker "$CLT/Frameworks" \
        -Xlinker -rpath -Xlinker "$CLT/usr/lib" "$@"
fi
exec swift test "$@"
