#!/usr/bin/env bash
# Verifies and installs the cached full Racket runtime at CI's existing Unix paths.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

version="${1:?Racket version is required}"
cache_dir="${2:?Installer cache directory is required}"
if [[ "$version" != 9.2 || "$(uname -s)-$(uname -m)" != Linux-x86_64 ]]; then
  echo "The CI Racket installer supports pinned Racket 9.2 on x86-64 Linux." >&2
  exit 1
fi

# Official release metadata: https://download.racket-lang.org/releases/9.2/installers/installers.json
installer_sha256="b029ae095d1f5d700dd0fcc237cb70d241e486308ba2499a2036cf751e7d46b3"
installer_name="racket-$version-x86_64-linux-buster-cs.sh"
installer="$cache_dir/$installer_name"
mkdir -p "$cache_dir"
if [[ ! -f "$installer" ]]; then
  curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
    "https://download.racket-lang.org/releases/$version/installers/$installer_name" \
    --output "$installer.part"
  printf '%s  %s\n' "$installer_sha256" "$installer.part" | sha256sum --check --status
  mv "$installer.part" "$installer"
fi
printf '%s  %s\n' "$installer_sha256" "$installer" | sha256sum --check --status

# Match the previous setup action's Unix-style installation and prompt answers.
printf 'yes\n1\n' | sudo sh "$installer" --create-dir --unix-style --dest /usr/
actual_version="$(racket -e '(display (version))')"
if [[ "$actual_version" != "$version" ]]; then
  echo "Racket version mismatch: expected $version, found $actual_version" >&2
  exit 1
fi
