#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
rid=${1:?Usage: verify.sh RID [INPUT.wma OUTPUT.ogg]}
work=${SAN9_BUILD_ROOT:-$root/.san9/build/$rid}
work=$(cd "$work" && pwd)
prefix=$work/install
deps=$work/deps
for component in AVDEVICE AVFILTER SWSCALE SWRESAMPLE NETWORK FFMPEG FFPLAY FFPROBE; do
    grep -qx "#define CONFIG_$component 0" "$work/ffmpeg/config.h" || {
        echo "Unexpected enabled build component: $component" >&2
        exit 1
    }
done
export PATH="$prefix/bin:$PATH"
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DYLD_LIBRARY_PATH="$prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
link=(-L"$prefix/lib" -lavformat -lavcodec -lavutil -L"$deps/lib" -lvorbisfile -lvorbis -logg -lm)
case "$rid" in
    win-*) link+=(-static-libgcc) ;;
    osx-*) link+=("-Wl,-rpath,$prefix/lib") ;;
esac
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$prefix/include" -isystem "$deps/include" "$root/tools/san9/verify.c" \
    "${link[@]}" -o "$work/verify-audio"
if [[ $# -eq 3 ]]; then
    "$work/verify-audio" "$2" "$3"
elif [[ $# -eq 1 ]]; then
    "$work/verify-audio" "$root/tests/san9/stereo.wma" "$work/stereo.ogg"
else
    echo "Usage: verify.sh RID [INPUT.wma OUTPUT.ogg]" >&2
    exit 1
fi
