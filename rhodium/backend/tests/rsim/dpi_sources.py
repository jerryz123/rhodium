# SPDX-License-Identifier: Apache-2.0
"""Check native implementations against generated DPI declarations without global compiler includes."""
from pathlib import Path


def dpi_checked_sources(directory, header, sources):
    directory.mkdir()
    checked = []
    for source in sources:
        source = Path(source).resolve()
        wrapper = directory / source.name
        wrapper.write_text(f'// SPDX-License-Identifier: Apache-2.0\n'
                           f'#include "{header}"\n#include "{source}"\n')
        checked.append(wrapper)
    return checked
