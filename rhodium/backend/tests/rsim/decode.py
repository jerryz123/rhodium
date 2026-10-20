# SPDX-License-Identifier: Apache-2.0
"""Exercise specialized record decoders and pooled tables in optimized native C++."""
import os
from pathlib import Path
import shlex
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def run_suite(work, run):
    run([str(ROOT / 'tools/run-racket.sh'),
         str(ROOT / 'rhodium/backend/tests/rsim-decode-test.rhm'), str(work)], work, 'decode-emit')
    driver = work / 'decode-main.cpp'
    driver.write_text('''// SPDX-License-Identifier: Apache-2.0
#include "Decode1.hpp"
#include "Decode2.hpp"
#include "Decode16.hpp"
#include "Dense.hpp"
#include <cassert>
#include <iostream>
int main() {
  rsim_pDecode1::Model first;
  rsim_pDecode2::Model second;
  rsim_pDecode16::Model invalid;
  for (unsigned route = 0; route != 256; ++route) {
    first.inputs.proute = route; second.inputs.proute = route;
    invalid.inputs.proute = route;
    first.eval(); second.tick(); invalid.eval();
    const auto a = first.outputs().presult;
    const auto b = second.outputs().presult;
    const auto c = invalid.outputs().presult;
    assert((unsigned(a.pvalid) << 8 | a.pmask) == (route < 16 ? ((256 + route) & 509) : 5));
    assert((unsigned(b.pvalid) << 8 | b.pmask) == (route < 16 ? ((272 + route) & 509) : 5));
    assert(c.pvalid == 0 && c.pmask == 5);
    auto copy = second;
    copy.inputs.proute = (route + 1) & 255;
    copy.tick();
    assert(second.outputs().presult.pmask == b.pmask);
  }
  rsim_pDense::Model dense;
  for (unsigned x = 0; x != 512; ++x) {
    dense.inputs.px = x | (UINT64_C(1) << 50);
    dense.tick();
    assert(dense.outputs().py == x * 17);
    assert(dense.outputs().pz == x * 17);
  }
  std::cout << "1280 specialized decode observations passed\\n";
}
''')
    sources = [work / f'Decode{site}.cpp' for site in (1, 2, 16)] + [work / 'Dense.cpp']
    if 'decode_table1' in (work / 'Dense.cpp').read_text():
        raise AssertionError('identical table storage was not shared')
    run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++20', '-O2',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
         *(str(path) for path in sources), str(driver), '-o', str(work / 'decode-test')],
        work, 'decode-build')
    output = run([str(work / 'decode-test')], work, 'decode-run')
    if '1280 specialized decode observations passed' not in output:
        raise AssertionError(output)
    return 1280


if __name__ == '__main__':
    from run import run
    work = Path(tempfile.mkdtemp(prefix='rsim-decode-'))
    print(f'{run_suite(work, run)} observations passed; artifacts: {work}')
