# Executes tools and encodes width-preserving stimulus tables shared by backend tests.
# SPDX-License-Identifier: Apache-2.0
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FIXTURES = Path(__file__).resolve().parent
WIDTHS = (1, 5, 65)

def vector_table(work, label, inputs, outputs, vectors, offset, masked=False):
    """Store stimuli and Python oracle bits as data, keeping the SV driver small."""
    row_width = sum(size for _, size in inputs + outputs)
    result_width = sum(size for _, size in outputs)
    mask_fields = [(f"mask_{label}_{i}", size) for i, (_, size) in enumerate(outputs)] if masked else []
    row_width += sum(size for _, size in mask_fields)
    rows = []
    for vector in vectors:
        stimulus, expected_values = vector[:2]
        known_masks = vector[2] if masked else ()
        row = 0
        for (_, size), value in zip(inputs + outputs + mask_fields, stimulus + expected_values + known_masks):
            assert 0 <= value < (1 << size)
            row = (row << size) | value
        assert len(stimulus) == len(inputs) and len(expected_values) == len(outputs)
        assert len(known_masks) == len(mask_fields)
        rows.append(f"{row:0{(row_width + 3) // 4}x}")
    data = work / f"{label}.hex"
    data.write_text("\n".join(rows) + "\n")
    declarations = [f"logic [{row_width-1}:0] cases_{label} [0:{len(rows)-1}];",
                    f"logic [{result_width-1}:0] expected_{label};"]
    actual = "{" + ", ".join(name for name, _ in outputs) + "}"
    targets_list = [name for name, _ in inputs] + [f"expected_{label}"]
    expected_result = f"expected_{label}"
    if masked:
        declarations.append(f"logic [{result_width-1}:0] known_{label};")
        targets_list.append(f"known_{label}")
        actual = f"({actual} & known_{label})"
        expected_result = f"(expected_{label} & known_{label})"
    targets = "{" + ", ".join(targets_list) + "}"
    # Verilator limits each display argument to 8192 bits. Keep all compared
    # bits in the transcript, splitting only its formatting into small chunks.
    display_format, display_args = "%h", actual
    if result_width > 4096:
        observed = f"observed_{label}"
        declarations.append(f"wire [{result_width-1}:0] {observed} = {actual};")
        chunks = [f"{observed}[{min(low + 4096, result_width)-1}:{low}]"
                  for low in reversed(range(0, result_width, 4096))]
        display_format, display_args = "%h" * len(chunks), ", ".join(chunks)
    statements = [f"$readmemh({json.dumps(str(data))}, cases_{label});",
                  f"for (int i = 0; i < {len(rows)}; i++) begin",
                  f"  {targets} = cases_{label}[i]; #1;",
                  f'  if ({actual} !== {expected_result}) $fatal(1, "{label}, case %0d", i);',
                  f'  $display("sample %0d {display_format}", {offset} + i, {display_args});',
                  "end"]
    return declarations, statements, len(rows)

def run(command, log, timeout=300):
    env = dict(os.environ, PLTCOLLECTS=str(ROOT) + os.pathsep)
    result = subprocess.run([str(arg) for arg in command], cwd=ROOT, env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}; see {log}")
    return result.stdout
