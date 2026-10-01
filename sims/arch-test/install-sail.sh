#!/usr/bin/env bash
# Packages the pinned Sail executable and embeddable model from build-local source.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$repo_dir/riscv/sail-riscv"
series="${SAIL_PATCH_SERIES:-$repo_dir/riscv/sail-riscv-patches/series}"
materializer="$repo_dir/riscv/patched_submodule.py"
python="${PYTHON:-python3}"
compiler_version=0.20.2
identity="$("$python" "$materializer" identity --repository "$repo_dir" --submodule riscv/sail-riscv --series "$series")"
install_dir="$repo_dir/.tools/sail-riscv-0.14.1-sail-$compiler_version-$identity"

if [[ -f "$install_dir/.complete" ]] && [[ "$(< "$install_dir/.complete")" == "$identity" ]] && \
   [[ -x "$install_dir/bin/sail_riscv_sim" ]] && \
   [[ -f "$install_dir/lib/cmake/SailModel/SailModelConfig.cmake" ]] && \
   [[ -f "$install_dir/lib/libsail_riscv_model.a" ]] && \
   [[ "$("$install_dir/bin/sail_riscv_sim" --version)" == 0.14.1 ]]; then
  echo "Patched Sail RISC-V $identity is already installed at $install_dir"
  exit 0
fi
if [[ -e "$install_dir" ]]; then
  echo "Sail install path contains an incomplete or different build: $install_dir" >&2
  exit 1
fi

for tool in "$python" git cmake; do
  command -v "$tool" >/dev/null || { echo "$tool is required to build Sail RISC-V" >&2; exit 1; }
done
if [[ ! -e "$source_dir/.git" ]]; then
  git -C "$repo_dir" submodule update --init riscv/sail-riscv
fi
expected_revision="$(git -C "$repo_dir" ls-files --stage riscv/sail-riscv | awk '{print $2}')"
actual_revision="$(git -C "$source_dir" rev-parse HEAD)"
if [[ "$actual_revision" != "$expected_revision" ]] || [[ -n "$(git -C "$source_dir" status --porcelain)" ]]; then
  echo "Sail submodule must be pristine at $expected_revision" >&2
  exit 1
fi

work_dir="$(mktemp -d /tmp/rhodium-sail-riscv.XXXXXX)"
trap 'rm -rf "$work_dir"' EXIT
compiler="${SAIL_COMPILER:-}"
if [[ -z "$compiler" ]]; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) asset=Linux-x86_64; digest=26b59bcab2d66e9f220d317dfe45f8b09170ed70e59a824553d6f525134d1ff6 ;;
    Linux-aarch64) asset=Linux-aarch64; digest=10428d1be9a2945a71f9855c81027c22d6a2895dbbcf2ce9a4f9640203d5067f ;;
    *) asset= ;;
  esac
  if [[ -n "$asset" ]]; then
    compiler_dir="$repo_dir/.tools/sail-compiler-$compiler_version"
    if [[ ! -x "$compiler_dir/bin/sail" ]]; then
      if [[ -e "$compiler_dir" ]]; then
        echo "Sail compiler install path is incomplete: $compiler_dir" >&2
        exit 1
      fi
      curl --fail --location "https://github.com/rems-project/sail/releases/download/$compiler_version-binary/sail-$asset.tar.gz" -o "$work_dir/compiler.tar.gz"
      actual_digest="$(shasum -a 256 "$work_dir/compiler.tar.gz" | cut -d ' ' -f 1)"
      [[ "$actual_digest" == "$digest" ]] || { echo "Sail compiler checksum mismatch" >&2; exit 1; }
      mkdir -p "$work_dir/compiler" "$(dirname "$compiler_dir")"
      tar -xzf "$work_dir/compiler.tar.gz" -C "$work_dir/compiler" --strip-components=1
      mv "$work_dir/compiler" "$compiler_dir"
    fi
    compiler="$compiler_dir/bin/sail"
  else
    compiler="$(command -v sail || true)"
  fi
fi
compiler="$(command -v "$compiler" || true)"
if [[ -z "$compiler" || ! -x "$compiler" ]]; then
  echo "Install the Sail $compiler_version compiler and set SAIL_COMPILER" >&2
  exit 1
fi
compiler="$(cd "$(dirname "$compiler")" && pwd)/$(basename "$compiler")"
compiler_report="$("$compiler" --version)"
if [[ ! "$compiler_report" =~ (^|[^0-9])0\.20\.2([^0-9]|$) ]]; then
  echo "Expected Sail compiler $compiler_version, got: $compiler_report" >&2
  exit 1
fi

"$python" "$materializer" materialize --source "$source_dir" --series "$series" --output "$work_dir/source"
cmake -S "$work_dir/source" -B "$work_dir/build" -DSAIL_BIN="$compiler" \
  -DCMAKE_BUILD_TYPE=Release -DDOWNLOAD_GMP=OFF -DENABLE_RISCV_TESTS=OFF \
  -DFIRST_PARTY_TESTS=OFF -DSTATIC=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build "$work_dir/build" --target sail_riscv_sim --parallel "${SAIL_BUILD_JOBS:-2}"
mkdir -p "$work_dir/install/bin" "$(dirname "$install_dir")"
install -m 755 "$work_dir/build/c_emulator/sail_riscv_sim" "$work_dir/install/bin/sail_riscv_sim"
model_include="$work_dir/install/include/sail-model"
mkdir -p "$model_include" "$work_dir/install/lib/cmake/SailModel" "$work_dir/install/share/sail-model"
install -m 644 "$work_dir/build/c_emulator/libriscv_model.a" "$work_dir/install/lib/libsail_riscv_model.a"
install -m 644 "$work_dir/build/sail_runtime/libsail_runtime.a" "$work_dir/install/lib/libsail_runtime.a"
install -m 644 "$work_dir/build/dependencies/softfloat/libsoftfloat.a" "$work_dir/install/lib/libsail_softfloat.a"
install -m 644 "$work_dir/build/sail_riscv_model.h" "$work_dir/source/c_emulator/riscv_platform_if.h" \
  "$work_dir/source/c_emulator/config_utils.h" "$model_include/"
compiler_library="$("$compiler" --dir)"
cp "$compiler_library"/lib/*.h "$model_include/"
cp -R "$work_dir/build/_deps/jsoncons-src/include/jsoncons" "$model_include/"
cp -R "$work_dir/build/_deps/jsoncons-src/include/jsoncons_ext" "$model_include/"
install -m 644 "$work_dir/build/sail_riscv_config_schema.json" "$work_dir/install/share/sail-model/"
install -m 644 "$repo_dir/sims/cosim/SailModelConfig.cmake" "$work_dir/install/lib/cmake/SailModel/"
install -m 644 "$work_dir/build/_deps/jsoncons-src/LICENSE" "$work_dir/install/share/sail-model/jsoncons-LICENSE"
cp -R "$compiler_library/lib" "$work_dir/install/share/sail-model/runtime-source"
install -m 644 "$source_dir/LICENCE" "$work_dir/install/LICENCE"
while IFS= read -r licence; do
  relative="${licence#"$source_dir"/}"
  mkdir -p "$work_dir/install/licences/$(dirname "$relative")"
  install -m 644 "$licence" "$work_dir/install/licences/$relative"
done < <(find "$source_dir/dependencies" -type f \( -iname 'LICENSE*' -o -iname 'LICENCE*' -o -iname 'COPYING*' \))
[[ "$("$work_dir/install/bin/sail_riscv_sim" --version)" == 0.14.1 ]]
printf '%s\n' "$identity" > "$work_dir/install/.complete"
mv "$work_dir/install" "$install_dir"
echo "Installed patched Sail RISC-V $identity at $install_dir"
