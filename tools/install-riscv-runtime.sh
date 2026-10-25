#!/usr/bin/env bash
# Builds pinned Newlib and libgcc for RV64GC/LP64D medany without rebuilding GCC.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cc="$(command -v "${1:?usage: install-riscv-runtime.sh COMPILER PREFIX}")"
prefix="${2:?usage: install-riscv-runtime.sh COMPILER PREFIX}"
revision=8ba4275b83ec27529f67e0d477611fa6d8d6e6bd
digest=8e27b243f9e677af3714e411fd9b7bdb687b2963218a08c48a109b5537e033cc
gcc_revision=6afcc4f6da931eb93f3ab001a0dd9650ea71d1ea
gcc_digest=b62f8feee8f9f6e3c9bb60e09546c869dacd2b1e2948a2da2c610ce76a3e5e89
target="$("$cc" -dumpmachine)"
[[ "$target" == riscv64-unknown-elf ]] || { echo 'The runtime requires an RV64 bare-metal compiler.' >&2; exit 1; }
gcc_version="$("$cc" -dumpfullversion)"
sdk="$("$cc" -print-file-name=plugin)/include"
[[ -f "$sdk/auto-host.h" && -f "$sdk/tm.h" ]] || {
  echo 'The installed GCC target headers are required to build libgcc.' >&2; exit 1;
}
if command -v sha256sum >/dev/null 2>&1; then
  checksum=(sha256sum)
else
  checksum=(shasum -a 256)
fi
identity="$( { cat "$0"; "$cc" --version; } | "${checksum[@]}" | awk '{print $1}')"
stamp="$prefix/.runtime-medany-sha256"
if [[ -f "$stamp" && "$(<"$stamp")" == "$identity" &&
      -f "$prefix/$target/lib/libc.a" && -f "$prefix/$target/lib/libm.a" &&
      -f "$prefix/lib/gcc/$target/$gcc_version/libgcc.a" ]]; then
  echo "Reusing medany C runtime at $prefix"
  exit 0
fi
work="$(mktemp -d /tmp/rhodium-runtime.XXXXXX)"
trap 'rm -rf "$work"' EXIT
curl --fail --location --retry 3 \
  "https://github.com/cygwin/cygwin/archive/$revision.tar.gz" -o "$work/source.tar.gz"
actual="$("${checksum[@]}" "$work/source.tar.gz" | awk '{print $1}')"
[[ "$actual" == "$digest" ]] || { echo 'Newlib source checksum mismatch.' >&2; exit 1; }
mkdir "$work/source" "$work/build" "$work/install"
tar -xzf "$work/source.tar.gz" -C "$work/source" --strip-components=1
export PATH="$(dirname "$cc"):$PATH"
make_command=make
if command -v gmake >/dev/null 2>&1; then make_command=gmake; fi
(
  cd "$work/build"
  "$work/source/configure" --target="$target" --prefix="$work/install" --disable-multilib \
    --enable-newlib-io-long-double --enable-newlib-io-long-long \
    --enable-newlib-io-c99-formats --enable-newlib-register-fini \
    CC_FOR_TARGET="$cc" \
    CFLAGS_FOR_TARGET='-O2 -D_POSIX_MODE -ffunction-sections -fdata-sections -march=rv64gc -mabi=lp64d -mcmodel=medany'
  "$make_command" --jobs="${BUILD_JOBS:-4}"
  "$make_command" install
)

curl --fail --location --retry 3 \
  "https://github.com/gcc-mirror/gcc/archive/$gcc_revision.tar.gz" -o "$work/gcc.tar.gz"
actual="$("${checksum[@]}" "$work/gcc.tar.gz" | awk '{print $1}')"
[[ "$actual" == "$gcc_digest" ]] || { echo 'GCC source checksum mismatch.' >&2; exit 1; }
mkdir "$work/gcc-source" "$work/libgcc-build"
tar -xzf "$work/gcc.tar.gz" -C "$work/gcc-source" --strip-components=1
mkdir -p "$work/libgcc-build/gcc" "$work/libgcc-build/$target/libgcc"
cp -R "$sdk/." "$work/libgcc-build/gcc/"
# libgcc's standalone Makefile consumes these two compiler-build variables and
# GCC's exported target headers. Generate tconfig.h with GCC's own generator.
printf '%s\n' 'GCC_CFLAGS = -DIN_GCC -DCROSS_DIRECTORY_STRUCTURE' \
  'INHIBIT_LIBC_CFLAGS = -Dinhibit_libc' > "$work/libgcc-build/gcc/libgcc.mvars"
(
  cd "$work/libgcc-build/gcc"
  DEFINES=USED_FOR_TARGET HEADERS=auto-host.h bash "$work/gcc-source/gcc/mkconfig.sh" tconfig.h
)
(
  cd "$work/libgcc-build/$target/libgcc"
  "$work/gcc-source/libgcc/configure" --host="$target" --target="$target" \
    --with-cross-host="$("$work/gcc-source/config.guess")" --with-target-subdir="$target" \
    --prefix="$work/install" --disable-shared --disable-multilib \
    CC="$cc" CFLAGS='-O2 -march=rv64gc -mabi=lp64d -mcmodel=medany'
  "$make_command" --jobs="${BUILD_JOBS:-4}" libgcc.a
)
mkdir -p "$work/install/lib/gcc/$target/$gcc_version"
cp "$work/libgcc-build/$target/libgcc/libgcc.a" "$work/install/lib/gcc/$target/$gcc_version/"
# Install matching headers and libraries together; retain the compiler and
# any existing non-default multilib directories in the destination prefix.
mkdir -p "$prefix"
cp -R "$work/install/." "$prefix/"
echo "$identity" > "$stamp"
