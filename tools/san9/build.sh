#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
rid=${1:?Usage: build.sh win-x64|linux-x64|osx-arm64}
case "$rid:$(uname -s):$(uname -m)" in
    win-x64:MINGW*:x86_64|win-x64:MSYS*:x86_64) platform=windows ;;
    linux-x64:Linux:x86_64) platform=linux ;;
    osx-arm64:Darwin:arm64) platform=macos ;;
    *) echo "Unsupported native build target: $rid on $(uname -sm)" >&2; exit 1 ;;
esac

work=${SAN9_BUILD_ROOT:-$root/.san9/build/$rid}
mkdir -p "$work"
work=$(cd "$work" && pwd)
prefix=$work/install
deps=$work/deps
jobs=${SAN9_JOBS:-4}
export LC_ALL=C
export PKG_CONFIG_LIBDIR=$deps/lib/pkgconfig
export PKG_CONFIG_PATH=$PKG_CONFIG_LIBDIR

ogg_version=1.3.5
ogg_sha=c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705
vorbis_version=1.3.7
vorbis_sha=b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b

fetch() {
    local name=$1 version=$2 sha=$3 archive="$work/lib$1-$2.tar.xz"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 "https://downloads.xiph.org/releases/$name/lib$name-$version.tar.xz" -o "$archive.partial"
        mv "$archive.partial" "$archive"
    fi
    if command -v sha256sum >/dev/null; then
        printf '%s  %s\n' "$sha" "$archive" | sha256sum --check -
    else
        printf '%s  %s\n' "$sha" "$archive" | shasum -a 256 --check -
    fi
    if [[ ! -d "$work/lib$name-$version" ]]; then
        tar -xJf "$archive" -C "$work"
    fi
}

fetch ogg "$ogg_version" "$ogg_sha"
fetch vorbis "$vorbis_version" "$vorbis_sha"
for name in "ogg-$ogg_version" "vorbis-$vorbis_version"; do
    cmake -S "$work/lib$name" -B "$work/$name-cmake" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$deps" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$deps" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DINSTALL_DOCS=OFF
    cmake --build "$work/$name-cmake" --parallel "$jobs"
    cmake --install "$work/$name-cmake"
done

options=(
    --prefix="$prefix"
    --disable-all
    --disable-autodetect
    --enable-avcodec
    --enable-avformat
    --enable-avutil
    --enable-shared
    --disable-static
    --enable-pic
    --enable-small
    --disable-debug
    --disable-doc
    --disable-programs
    --disable-network
    --disable-gpl
    --disable-nonfree
    --disable-version3
    --enable-protocol=file
    --enable-demuxer=asf
    --enable-decoder=wmav2
    --enable-libvorbis
    --enable-encoder=libvorbis
    --enable-muxer=ogg
    --pkg-config-flags=--static
)
case "$platform" in
    windows) options+=(--disable-pthreads --extra-ldflags=-static-libgcc) ;;
    linux) options+=("--extra-ldflags=-Wl,-rpath,'\$\$ORIGIN'") ;;
    macos) options+=(--install-name-dir=@rpath) ;;
esac
mkdir -p "$work/ffmpeg"
(
    cd "$work/ffmpeg"
    "$root/configure" "${options[@]}"
    make -j "$jobs"
    make install
)

mkdir -p "$prefix/share/san9/licenses"
cp "$root/COPYING.LGPLv2.1" "$prefix/share/san9/licenses/FFmpeg-LGPL-2.1.txt"
cp "$root/LICENSE.md" "$prefix/share/san9/licenses/FFmpeg-LICENSE.md"
cp "$work/libogg-$ogg_version/COPYING" "$prefix/share/san9/licenses/libogg-COPYING.txt"
cp "$work/libvorbis-$vorbis_version/COPYING" "$prefix/share/san9/licenses/libvorbis-COPYING.txt"
{
    printf 'FFmpeg source: https://github.com/cdd1037/FFmpeg\n'
    printf 'FFmpeg commit: %s\n' "$(git -C "$root" rev-parse HEAD)"
    printf 'Target: %s\n' "$rid"
    printf 'libogg: %s sha256=%s\n' "$ogg_version" "$ogg_sha"
    printf 'libvorbis: %s sha256=%s\n' "$vorbis_version" "$vorbis_sha"
    printf 'Configure:'
    printf ' %q' "${options[@]}"
    printf '\n'
    "${CC:-cc}" --version
} > "$prefix/share/san9/BUILD.txt"

bash "$root/tools/san9/verify.sh" "$rid"
echo "Verified libraries: $prefix"
