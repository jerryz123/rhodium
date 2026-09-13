#!/usr/bin/env python3
# Runs pinned ACT with the repository's Sail 0.14 requirement for configurable ASIDs.
# SPDX-License-Identifier: Apache-2.0
from act import config
from act.act import main

if __name__ == "__main__":
    # Upstream ACT still pins 0.13.1. Keep its check, but require our tested pin.
    config.REQUIRED_SAIL_VERSION = "0.14"
    main()
