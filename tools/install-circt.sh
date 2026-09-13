#!/usr/bin/env bash
# Installs the pinned official CIRCT toolchain used by Rhodium backend tests.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
version="1.155.0"
install_root="$repo_dir/.tools"
install_dir="$install_root/firtool-$version"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)
    archive_name="circt-full-shared-macos-arm64.tar.gz"
    archive_sha256="af4bd7bf021c45425bead9198a84d0a62aebb3472f5644821c0cdfb87811d91c"
    ;;
  Linux-x86_64)
    archive_name="circt-full-shared-linux-x64.tar.gz"
    archive_sha256="bc3d22cfa4ee34d93141ad2b2193be4fa482f79f6606a24bbcb10b62898df1db"
    ;;
  *)
    echo "The bootstrap script supports x86-64 Linux and Apple Silicon macOS." >&2
    echo "Install CIRCT separately and set CIRCT_OPT to its circt-opt executable." >&2
    exit 1
    ;;
esac

archive_url="https://github.com/llvm/circt/releases/download/firtool-$version/$archive_name"

if [[ -x "$install_dir/bin/circt-opt" ]]; then
  echo "CIRCT $version is already installed at $install_dir"
  exit 0
fi

download_dir="$(mktemp -d /tmp/rhodium-circt-download.XXXXXX)"
trap 'rm -rf "$download_dir"' EXIT
archive_path="$download_dir/$archive_name"

curl --fail --location "$archive_url" --output "$archive_path"
if command -v sha256sum >/dev/null 2>&1; then
  actual_sha256="$(sha256sum "$archive_path" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  actual_sha256="$(shasum -a 256 "$archive_path" | awk '{print $1}')"
else
  echo "sha256sum or shasum is required to verify the CIRCT archive" >&2
  exit 1
fi
if [[ "$actual_sha256" != "$archive_sha256" ]]; then
  echo "CIRCT archive checksum mismatch" >&2
  exit 1
fi

mkdir -p "$install_root"
tar -xzf "$archive_path" -C "$install_root"
echo "Installed CIRCT $version at $install_dir"
