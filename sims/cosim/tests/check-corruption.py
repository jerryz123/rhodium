# SPDX-License-Identifier: Apache-2.0
import subprocess
import sys

result = subprocess.run([sys.argv[1], *sys.argv[3:], "+cosim-corrupt-order=0", sys.argv[2]],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
if result.returncode == 0 or "cosim mismatch:" not in result.stdout or "GPR value" not in result.stdout:
    raise SystemExit("corrupted GPR did not produce a checker mismatch:\n" + result.stdout)
print("Sail cosim: deliberately corrupted GPR was rejected")
