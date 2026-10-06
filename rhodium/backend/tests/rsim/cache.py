#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Count actual region executions across idle ticks and independent active cones."""
import os
from pathlib import Path
import re
import shlex
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def run_suite(work, run):
    work.mkdir(parents=True, exist_ok=True)
    settings = {'RHODIUM_RSIM_TEST_REGION_BUDGET': '1', 'RHODIUM_RSIM_TEST_CACHE': '1'}
    previous = {key: os.environ.get(key) for key in settings}
    try:
        os.environ.update(settings)
        run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-cache.rhm'), str(work)], work, 'emit')
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    source = (work / 'RsimCache.cpp').read_text()
    source, helpers = re.subn(r'(void Model::(?:pre|output)_region\d+\([^\n]*const State& current\) const \{)',
                             r'\1\n  ++pure_calls;', source)
    source, products = re.subn(r'(?m)^([^\n]*\(v\d+ \* v\d+\)[^\n]*;)$',
                              r'  ++product_calls;\n\1', source)
    assert helpers > 4 and products == 4, (helpers, products)
    (work / 'instrumented.cpp').write_text('extern unsigned pure_calls, product_calls;\n' + source)
    (work / 'main.cpp').write_text(r'''
#include "RsimCache.hpp"
#include <cassert>
#include <iostream>
unsigned pure_calls = 0, product_calls = 0;
int main() {
  rsim_pRsimCache::Model dut;
  dut.inputs.pa = 3; dut.inputs.pb = 7;
  dut.tick(); dut.tick(); // Settle both phase caches and captured results.
  pure_calls = product_calls = 0;
  for (unsigned i = 0; i < 100; ++i) { dut.eval(); dut.tick(); }
  assert(pure_calls == 0 && product_calls == 0);
  assert(dut.outputs().px == 9 && dut.outputs().py == 49);
  dut.inputs.pa = 4; dut.eval();
  assert(product_calls == 1 && dut.outputs().px == 16 && dut.outputs().psaved_ux == 9);
  dut.eval(); assert(product_calls == 1);
  dut.tick(); assert(product_calls == 2 && dut.outputs().psaved_ux == 16);
  dut.tick();
  pure_calls = product_calls = 0;
  dut.inputs.penable = 1;
  for (unsigned i = 0; i < 100; ++i) dut.tick();
  assert(pure_calls > 0 && product_calls == 0 && dut.outputs().pcount == 100);
  // Copies retain owned caches independently; changing one cannot dirty another.
  auto copied = dut;
  copied.inputs.pb = 9; copied.tick();
  assert(copied.outputs().py == 81 && dut.outputs().py == 49);
  dut.inputs.preset = 1; dut.tick();
  assert(dut.outputs().pcount == 0 && dut.outputs().psaved_ux == 0);
  dut.inputs.preset = 0; dut.tick();
  assert(dut.outputs().pcount == 1 && dut.outputs().psaved_ux == 16);
  std::cout << "idle: zero pure helpers over 100 ticks; active counter: zero datapath multiplies\n";
}
''')
    run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(work / 'instrumented.cpp'),
        str(work / 'main.cpp'), '-o', str(work / 'test')], work, 'build')
    print(run([str(work / 'test')], work, 'execute'), end='')

    run_borrow_suite(work, run)


def run_borrow_suite(work, run):
    source = (work / 'RsimCacheBorrows.cpp').read_text()
    source, helpers = re.subn(r'(void Model::output_region\d+\([^\n]*const State& current\) const \{)',
                             r'\1\n  ++borrow_pure_calls;', source)
    assert helpers > 3, helpers
    (work / 'borrows-instrumented.cpp').write_text('extern unsigned borrow_pure_calls;\n' + source)
    (work / 'borrows-main.cpp').write_text(r'''
#include "RsimCacheBorrows.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
unsigned borrow_pure_calls = 0;
using Model = rsim_pRsimCacheBorrows::Model;
static std::uint32_t decoded(unsigned select) {
  const std::uint32_t values[] = {0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d};
  return values[select & 3];
}
template <typename T> static std::uint32_t packed(const T& a) {
  return (std::uint32_t(a[1].px) << 24) | (std::uint32_t(a[1].py) << 16)
       | (std::uint32_t(a[0].px) << 8) | a[0].py;
}
static void payload(Model& dut, std::uint32_t value) {
  dut.inputs.ppayload[0].py = value & 255; dut.inputs.ppayload[0].px = (value >> 8) & 255;
  dut.inputs.ppayload[1].py = (value >> 16) & 255; dut.inputs.ppayload[1].px = value >> 24;
}
static void check(Model& dut, std::uint32_t state, std::uint32_t captured) {
  const auto& out = dut.outputs();
  const auto row = decoded(dut.inputs.pselect);
  const auto chain = dut.inputs.pchoose ? 0xaabbccddu : row;
  assert(packed(out.pdirect) == state && packed(out.pcaptured) == captured);
  assert(out.pfirst.px == ((state >> 8) & 255) && out.pfirst.py == (state & 255));
  assert(packed(out.pdecoded.planes) == row && packed(out.pprojected) == row);
  assert(out.pdecoded.pflag == (dut.inputs.pselect & 1));
  assert(packed(out.pchain) == chain);
  assert(packed(out.pborrowed) == (dut.inputs.pchoose ? chain : state));
}
int main() {
  Model dut;
  std::uint32_t state = 0, captured = 0;
  dut.eval(); check(dut, state, captured);
  for (unsigned i = 0; i < 96; ++i) {
    dut.inputs.pselect = (i / 3) & 3; dut.inputs.pchoose = (i / 12) & 1;
    dut.inputs.pupdate = i % 3 == 2; dut.inputs.preset = i % 17 == 0;
    const auto data = 0x12345678u + i * 0x03050709u;
    payload(dut, data);
    dut.eval(); check(dut, state, captured);
    borrow_pure_calls = 0;
    dut.eval(); check(dut, state, captured);
    // Only the mixed state/constant pointer helper remains eager in this phase.
    assert(borrow_pure_calls == 1);
    captured = dut.inputs.preset ? 0 : (dut.inputs.pchoose ? 0xaabbccddu : state);
    state = dut.inputs.preset ? 0 : (dut.inputs.pupdate ? data : state);
    dut.tick(); check(dut, state, captured);
    // A move-assigned copy must rebind mutable pointers to its own state bank.
    Model copy;
    { auto temporary = dut; copy = std::move(temporary); }
    dut.inputs.pupdate = 1; dut.inputs.preset = 0; payload(dut, ~data);
    dut.tick();
    captured = dut.inputs.pchoose ? 0xaabbccddu : state;
    state = ~data;
    check(dut, state, captured);
    const auto copy_state = packed(copy.outputs().pdirect);
    const auto copy_captured = packed(copy.outputs().pcaptured);
    copy.eval(); check(copy, copy_state, copy_captured);
    copy.inputs.pchoose = 0; copy.eval(); check(copy, copy_state, copy_captured);
  }
  // Immutable decode pointers remain valid after the originating model dies.
  Model survivor;
  { Model temporary; temporary.inputs.pselect = 2; temporary.eval(); survivor = temporary; }
  survivor.eval(); check(survivor, 0, 0);
  survivor.inputs.pselect = 3; survivor.eval(); check(survivor, 0, 0);
  std::cout << "borrow lifetimes: banks, decode defaults/selectors, projections, copies and moves passed\n";
}
''')
    run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(work / 'borrows-instrumented.cpp'),
        str(work / 'borrows-main.cpp'), '-o', str(work / 'borrows-test')], work, 'borrows-build')
    print(run([str(work / 'borrows-test')], work, 'borrows-execute'), end='')


if __name__ == '__main__':
    from run import run
    work = Path(tempfile.mkdtemp(prefix='rsim-cache-'))
    run_suite(work, run)
    print(f'cache fixture artifacts: {work}')
