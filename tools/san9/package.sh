#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
rid=${1:?Usage: package.sh RID}
work=${SAN9_BUILD_ROOT:-$root/.san9/build/$rid}
work=$(cd "$work" && pwd)
prefix=$work/install
stage=$(mktemp -d "$work/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
native=$stage/runtimes/$rid/native
mkdir -p "$native" "$stage/share" "$root/.san9/packages"
case "$rid" in
    win-x64)
        cp "$prefix"/bin/avcodec-*.dll "$prefix"/bin/avformat-*.dll "$prefix"/bin/avutil-*.dll "$native/"
        mkdir -p "$stage/lib"
        cp "$prefix"/lib/libavcodec.dll.a "$prefix"/lib/libavformat.dll.a "$prefix"/lib/libavutil.dll.a "$stage/lib/"
        ;;
    linux-x64)
        cp -P "$prefix"/lib/libavcodec.so* "$prefix"/lib/libavformat.so* "$prefix"/lib/libavutil.so* "$native/"
        ;;
    osx-arm64)
        cp -P "$prefix"/lib/libavcodec*.dylib "$prefix"/lib/libavformat*.dylib "$prefix"/lib/libavutil*.dylib "$native/"
        ;;
    *) echo "Unsupported package target: $rid" >&2; exit 1 ;;
esac
cp -R "$prefix/include" "$stage/include"
cp -R "$prefix/share/san9" "$stage/share/san9"
tar -czf "$root/.san9/packages/san9-audio-$rid.tar.gz" -C "$stage" .
echo "Package: $root/.san9/packages/san9-audio-$rid.tar.gz"
