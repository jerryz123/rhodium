#!/usr/bin/env bash
# Resolves one OpenSBI producer's descriptor and output directory for Moon stages.
# SPDX-License-Identifier: Apache-2.0
source sw/build/moon-env.sh
if [[ -z ${RHODIUM_ARCHIVE_KEY:-} ]]; then
  output="${OPENSBI_OUTPUT:-${OPENSBI_BUILD_ROOT:-/tmp/rhodium-software}/$build_soc/opensbi}"
fi
mkdir -p "$output"
output="$(cd "$output" && pwd)"
stage="${RHODIUM_OPENSBI_STAGE:-$output}"
mkdir -p "$stage"
