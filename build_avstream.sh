#!/bin/bash
# Build minimal FFmpeg + libvpx + libopus for av_stream module
# Usage: ./build_avstream.sh [x11_32|x11_64|windows_32|windows_64|osx_32|osx_64]
# Leave blank for native linux build

set -e

# ---------------------------------------------------------------------------
# Version pins — set to empty string to use latest git HEAD
# ---------------------------------------------------------------------------
VPX_VERSION="v1.14.0"
OPUS_VERSION="v1.5.2"
FFMPEG_VERSION="n4.4.4"

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------
BASEDIR="$(cd "$(dirname "$0")" && pwd)"
TARGETARCH="${1:-x11_64}"
JOBS=$(nproc)

OUTDIR="$BASEDIR/output/$TARGETARCH"
EXPORTDIR="$BASEDIR/export/$TARGETARCH"
SOURCES="$BASEDIR/.sources"
CODEC_PREFIX="$OUTDIR"

echo "==> Building for: $TARGETARCH"
echo "==> Output:       $OUTDIR"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
clone_or_update() {
	local url="$1"
	local dir="$2"
	local version="$3"

	if [ ! -d "$dir/.git" ]; then
		echo "==> Cloning $url"
		if [ -n "$version" ]; then
			git clone --depth=1 --branch "$version" "$url" "$dir"
		else
			git clone --depth=1 "$url" "$dir"
		fi
	else
		echo "==> Updating $dir"
		cd "$dir"
		if [ -n "$version" ]; then
			# Fetch the tag if not present
			git fetch --depth=1 origin "refs/tags/$version:refs/tags/$version" 2>/dev/null || true
			git checkout "$version"
		else
			git pull --rebase
		fi
		cd "$BASEDIR"
	fi
}

# ---------------------------------------------------------------------------
# Toolchain setup per target
# ---------------------------------------------------------------------------
EXTRA_CONFIGURE=""
CROSS_PREFIX=""
VPX_TARGET=""
OPUS_HOST=""
PKG_CONFIG_PATH_EXTRA=""

case "$TARGETARCH" in
	windows_32)
		CROSS_PREFIX="i686-w64-mingw32-"
		VPX_TARGET="x86-win32-gcc"
		OPUS_HOST="i686-w64-mingw32"
		EXTRA_CONFIGURE="
		--cross-prefix=i686-w64-mingw32-
		--target-os=mingw32
		--arch=x86
		--disable-asm
		--extra-ldflags=-static-libgcc
		--extra-ldflags=-static
		--extra-libs=-lpthread"
		;;
	windows_64)
		CROSS_PREFIX="x86_64-w64-mingw32-"
		VPX_TARGET="x86_64-win64-gcc"
		OPUS_HOST="x86_64-w64-mingw32"
		EXTRA_CONFIGURE="
		--cross-prefix=x86_64-w64-mingw32-
		--target-os=mingw32
		--arch=x86_64
		--disable-asm
		--extra-ldflags=-static-libgcc
		--extra-ldflags=-static
		--extra-libs=-lpthread"
		;;
	osx_32)
		CROSS_PREFIX="i386-apple-darwin9-"
		VPX_TARGET="x86-darwin9-gcc"
		OPUS_HOST="i386-apple-darwin9"
		EXTRA_CONFIGURE="
		--cross-prefix=i386-apple-darwin9-
		--target-os=darwin
		--arch=x86
		--cc=i386-apple-darwin9-clang
		--cxx=i386-apple-darwin9-clang++-gstdc++
		--extra-libs=$OSXCROSS_ROOT/target/lib/gcc/x86_64-apple-darwin9/10.5.0/i386/libgcc.a
		--host-cc=gcc
		--disable-asm
		--extra-cflags=-std=c11"
		;;
	osx_64)
		CROSS_PREFIX="x86_64-apple-darwin9-"
		VPX_TARGET="x86_64-darwin9-gcc"
		OPUS_HOST="x86_64-apple-darwin9"
		EXTRA_CONFIGURE="
		--cross-prefix=x86_64-apple-darwin9-
		--target-os=darwin
		--arch=x86_64
		--cc=x86_64-apple-darwin9-clang
		--cxx=x86_64-apple-darwin9-clang++-gstdc++
		--host-cc=gcc
		--disable-asm
		--extra-cflags=-std=c11"
		;;
	x11_32)
		VPX_TARGET="x86-linux-gcc"
		OPUS_HOST="i686-linux-gnu"
		EXTRA_CONFIGURE="
		--arch=x86
		--extra-cflags=-m32
		--extra-ldflags=-m32"
		;;
	x11_64)
		VPX_TARGET="x86_64-linux-gcc"
		OPUS_HOST="x86_64-linux-gnu"
		EXTRA_CONFIGURE="
		--arch=x86_64"
		;;
	android_arm64v8)
		export NDK="/usr/lib/android-sdk/ndk/28.1.13356709"
		export TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
		export CROSS_PREFIX="$TOOLCHAIN/bin/aarch64-linux-android"
		export SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
		VPX_TARGET="arm64-android-gcc"
		OPUS_HOST="aarch64-linux-android"
		EXTRA_CONFIGURE+=(--cross-prefix="$CROSS_PREFIX"-)
		EXTRA_CONFIGURE+=(--target-os=android)
		EXTRA_CONFIGURE+=(--arch=aarch64)
		EXTRA_CONFIGURE+=(--cc="$CROSS_PREFIX"21-clang)
		EXTRA_CONFIGURE+=(--cxx="$CROSS_PREFIX"21-clang++)
		EXTRA_CONFIGURE+=(--sysroot="$SYSROOT")
		EXTRA_CONFIGURE+=(--enable-cross-compile)
		EXTRA_CONFIGURE+=(--disable-asm)
		EXTRA_CONFIGURE+=(--extra-cflags="-fPIC")
		EXTRA_CONFIGURE+=(--extra-ldflags="-fPIC")
		EXTRA_CONFIGURE+=(--extra-cflags="-fPIC -fpic -isystem $SYSROOT/usr/include -isystem $SYSROOT/usr/include/aarch64-linux-android")
		;;
	android_armv7)
		export NDK="/usr/lib/android-sdk/ndk/28.1.13356709"
		export TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
		export CROSS_PREFIX="$TOOLCHAIN/bin/armv7a-linux-androideabi"
		export SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
		VPX_TARGET="armv7-android-gcc"
		OPUS_HOST="armv7a-linux-androideabi"
		EXTRA_CONFIGURE+=(--cross-prefix="$TOOLCHAIN/bin/arm-linux-androideabi"-)
		EXTRA_CONFIGURE+=(--target-os=android)
		EXTRA_CONFIGURE+=(--arch=arm)
		EXTRA_CONFIGURE+=(--cpu=armv7-a)
		EXTRA_CONFIGURE+=(--cc="$CROSS_PREFIX"21-clang)
		EXTRA_CONFIGURE+=(--cxx="$CROSS_PREFIX"21-clang++)
		EXTRA_CONFIGURE+=(--sysroot="$SYSROOT")
		EXTRA_CONFIGURE+=(--enable-cross-compile)
		EXTRA_CONFIGURE+=(--disable-asm)
		EXTRA_CONFIGURE+=(--extra-cflags="-fPIC -mfpu=vfpv3-d16 -mfloat-abi=softfp")
		EXTRA_CONFIGURE+=(--extra-ldflags="-fPIC")
		EXTRA_CONFIGURE+=(--extra-cflags="-fPIC -fpic -D__ANDROID_MIN_SDK_VERSION__=21 -isystem $SYSROOT/usr/include -isystem $SYSROOT/usr/include/arm-linux-androideabi")
		;;
	*)
		echo "Unknown target: $TARGETARCH"
		echo "Usage: $0 [x11_32|x11_64|windows_32|windows_64|osx_32|osx_64|android_arm64v8|android_armv7]"
		exit 1
		;;
esac

mkdir -p "$OUTDIR" "$EXPORTDIR" "$SOURCES"
mkdir -p "$OUTDIR/include" "$OUTDIR/lib"

CODEC_PREFIX="$SOURCES/$TARGETARCH"
mkdir -p "$CODEC_PREFIX"

# ---------------------------------------------------------------------------
# Build libvpx
# ---------------------------------------------------------------------------
echo ""
echo "==> Building libvpx $VPX_VERSION"
VPX_SRC="$SOURCES/libvpx"

if [ -f "$CODEC_PREFIX/lib/libvpx.a" ]; then
    echo "==> libvpx already built, skipping"
else
	clone_or_update "https://chromium.googlesource.com/webm/libvpx" "$VPX_SRC" "$VPX_VERSION"

	cd "$VPX_SRC"
	make distclean 2>/dev/null || true

	EXTRACONFIG=
	if [[ "$TARGETARCH" == android* ]]; then
		if [[ "$TARGETARCH" == "android_armv7" ]]; then
			export CFLAGS="-I$SYSROOT/usr/include -I$SYSROOT/usr/include/arm-linux-androideabi -O2 -fPIC"
			EXTRACONFIG="--disable-neon \
				--disable-neon-dotprod \
				--disable-neon-i8mm \
				--disable-runtime-cpu-detect"
		elif [[ "$TARGETARCH" == "android_arm64v8" ]]; then
			export CFLAGS="-I$SYSROOT/usr/include -I$SYSROOT/usr/include/aarch64-linux-android -O2 -fPIC"
		fi
		export CC="${CROSS_PREFIX}21-clang"
		export CXX="${CROSS_PREFIX}21-clang++"
		export AR="$TOOLCHAIN/bin/llvm-ar"
		export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
		export LD="$TOOLCHAIN/bin/ld"
	else

		export CC=${CROSS_PREFIX}gcc
		export CXX=${CROSS_PREFIX}g++
		export AR=${CROSS_PREFIX}ar
		export CROSS="$CROSS_PREFIX"
	fi

	echo $CC
	./configure \
		--target="$VPX_TARGET" \
		--prefix="$CODEC_PREFIX" \
		--disable-shared \
		--enable-static \
		--disable-examples \
		--disable-tools \
		--disable-docs \
		--disable-unit-tests \
		--disable-runtime-cpu-detect \
		$EXTRACONFIG

	make -j"$JOBS"
	make install
	cp -r "$CODEC_PREFIX/include/vpx" "$OUTDIR/include/"
	cp "$CODEC_PREFIX/lib/libvpx.a" "$OUTDIR/lib/"
fi
echo "==> libvpx done"

# ---------------------------------------------------------------------------
# Build libopus
# ---------------------------------------------------------------------------
echo ""
echo "==> Building libopus $OPUS_VERSION"
OPUS_SRC="$SOURCES/opus"

if [ -f "$CODEC_PREFIX/lib/libopus.a" ]; then
    echo "==> libopus already built, skipping"
else
	clone_or_update "https://gitlab.xiph.org/xiph/opus.git" "$OPUS_SRC" "$OPUS_VERSION"

	cd "$OPUS_SRC"
	# opus needs autogen if configure doesn't exist
	[ -f configure ] || ./autogen.sh

	make distclean 2>/dev/null || true

	EXTRACONFIG=
	if [[ "$TARGETARCH" == android* ]]; then
		if [[ "$TARGETARCH" == "android_armv7" ]]; then
			export CFLAGS="-I$SYSROOT/usr/include -I$SYSROOT/usr/include/arm-linux-androideabi -O2 -fPIC"
		elif [[ "$TARGETARCH" == "android_arm64v8" ]]; then
			export CFLAGS="-I$SYSROOT/usr/include -I$SYSROOT/usr/include/aarch64-linux-android -O2 -fPIC"
		fi
		export CC="${CROSS_PREFIX}21-clang"
		export CXX="${CROSS_PREFIX}21-clang++"
		export AR="$TOOLCHAIN/bin/llvm-ar"
		export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
		export NM="$TOOLCHAIN/bin/nm"
		EXTRACONFIG="--disable-asm"
	else

		export CC=${CROSS_PREFIX}gcc
		export CXX=${CROSS_PREFIX}g++
		export AR=${CROSS_PREFIX}ar
		export RANLIB=${CROSS_PREFIX}ranlib \
		export NM=${CROSS_PREFIX}nm
	fi
	./configure \
		--host="$OPUS_HOST" \
		--prefix="$CODEC_PREFIX" \
		--disable-shared \
		--enable-static \
		--disable-extra-programs \
		--disable-doc \
		$EXTRACONFIG

	make -j"$JOBS"
	make install
	cp -r "$CODEC_PREFIX/include/opus" "$OUTDIR/include/"
	cp "$CODEC_PREFIX/lib/libopus.a" "$OUTDIR/lib/"
fi
echo "==> libopus done"

# ---------------------------------------------------------------------------
# Build FFmpeg
# ---------------------------------------------------------------------------
echo ""
echo "==> Building FFmpeg $FFMPEG_VERSION"
FFMPEG_SRC="$SOURCES/ffmpeg"

if [ -f "$OUTDIR/lib/libavformat.so" ] || [ -f "$OUTDIR/lib/libavformat.dylib" ] || [ -f "$OUTDIR/bin/avformat-58.dll" ]; then
    echo "==> FFmpeg already built in $OUTDIR, skipping"
else
	clone_or_update "https://git.ffmpeg.org/ffmpeg.git" "$FFMPEG_SRC" "$FFMPEG_VERSION"

	cd "$FFMPEG_SRC"
	make distclean 2>/dev/null || true

	PKG_CONFIG_PATH="$CODEC_PREFIX/lib/pkgconfig" \
		./configure \
		--prefix="$OUTDIR" \
		--enable-shared \
		--disable-static \
		--disable-programs \
		--disable-doc \
		--disable-everything \
		--disable-asm \
		--enable-avformat \
		--enable-avutil \
		--enable-network \
		--enable-protocol=tcp \
		--enable-protocol=udp \
		--enable-protocol=rtp \
		--enable-protocol=file \
		--enable-demuxer=mpegts \
		--enable-demuxer=rtp \
		--enable-demuxer=rtsp \
		--enable-demuxer=sdp \
		--enable-parser=vp8 \
		--enable-parser=vp9 \
		--enable-parser=opus \
		--disable-gpl \
		--disable-nonfree \
		$EXTRA_CONFIGURE

	make -j"$JOBS"
	make install
fi

echo "==> Copying shared libs to $EXPORTDIR"
cp -P "$OUTDIR"/lib/*.so* "$EXPORTDIR/" 2>/dev/null || true
cp -P "$OUTDIR"/lib/*.dylib "$EXPORTDIR/" 2>/dev/null || true
cp "$OUTDIR"/bin/*.dll "$EXPORTDIR/" 2>/dev/null || true

echo "==> Export done: $EXPORTDIR"

echo ""
echo "==> Done! FFmpeg libs are in $OUTDIR/lib/"
echo "==> Static codec libs are in $CODEC_PREFIX/lib/"
echo "==> Add these to your Godot module's ffmpeg/$TARGETARCH/ folder"
