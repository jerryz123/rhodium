# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from riscv.patched_submodule import materialize


class BenchmarkInputsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.cc = shutil.which('cc')
        if not self.cc:
            self.skipTest('native C compiler required')

    def source(self, variable, default, marker):
        path = Path(os.environ.get(variable, str(ROOT / default))).resolve()
        if not (path / marker).is_file():
            self.skipTest(f'initialize {default} or set {variable}')
        return path

    def compile_run(self, text, flags=(), sources=(), cxx=False):
        source = self.work / ('check.cc' if cxx else 'check.c')
        source.write_text(text)
        executable = self.work / 'check'
        subprocess.run([shutil.which('c++') if cxx else self.cc, '-O2',
                        '-std=c++17' if cxx else '-std=gnu11', *map(str, flags),
                        str(source), *map(str, sources), '-o', str(executable)],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(executable)], check=True)

    def test_highway_exact_inputs(self):
        self.compile_run(r'''
#include "constant-inputs.h"
#include <cassert>
template <class T> void check() {
  alignas(64) static constexpr auto left = DotInputs<T, 17, 8>();
  alignas(64) static constexpr auto right = DotInputs<T, 7, 4>();
  alignas(64) static constexpr auto delta = DeltaInputs<T>();
  for (uint32_t i = 0; i < 257; ++i) {
    uint32_t v = i + 0x9e3779b9u;
    v = (v ^ (v >> 16)) * 0x85ebca6bu;
    v = (v ^ (v >> 13)) * 0xc2b2ae35u;
    v ^= v >> 16;
    assert(OpaqueInput(left)[i] == T(float(i % 17) * 0.125f));
    assert(OpaqueInput(right)[i] == T(float(i % 7) * 0.25f));
    assert(OpaqueInput(delta)[i] == T(v % 127));
  }
}
int main() { check<float>(); check<double>(); check<int16_t>(); check<int32_t>(); }
''', flags=['-I' + str(ROOT / 'sw/highway-riscv-baremetal')], cxx=True)

    def test_packet_fields_match_original_generator(self):
        source = self.source('RHODIUM_BRINGUP_SOURCE', 'sw/bringup-bench', 'packet-filter/packet-filter.c')
        patched = materialize(source, ROOT / 'sw/bringup-bench-patches/series', self.work / 'source')
        self.compile_run(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define main upstream_main
#include "packet-filter/packet-filter.c"
#undef main
#include "packets.h"
int libmin_printf(char *fmt, ...) { return 0; }
void libmin_fail(int status) { exit(status); }
void libmin_success(void) { exit(0); }
int main(void) {
  libmin_srand(42);
  for (int i = 0; i < PACKET_COUNT; ++i) {
    Packet expected = generate_packet();
    assert(expected.src_ip == packets[i].src_ip);
    assert(expected.dest_ip == packets[i].dest_ip);
    assert(expected.src_port == packets[i].src_port);
    assert(expected.dest_port == packets[i].dest_port);
    assert(expected.protocol == packets[i].protocol);
    assert(memcmp(expected.payload, packets[i].payload, 64) == 0);
  }
}
''', flags=['-U__clang__', '-DTARGET_HOST', '-I' + str(source),
            '-I' + str(source / 'common'), '-I' + str(source / 'target'),
            '-I' + str(patched / 'packet-filter')], sources=[source / 'common/libmin_rand.c'])

    def test_crc_bytes_match_original_prng(self):
        source = self.source('RHODIUM_EMBENCH_SOURCE', 'sw/embench-iot', 'src/crc32/crc_32.c')
        patched = materialize(source, ROOT / 'sw/embench-iot-patches/series', self.work / 'source')
        self.compile_run(r'''
#include <assert.h>
#include "beebsc.h"
#include "crc-input.h"
int main(void) {
  srand_beebs(0);
  for (int i = 0; i < 1024; ++i)
    assert(crc_input[i] == (unsigned char)rand_beebs());
}
''', flags=['-I' + str(source / 'support'), '-I' + str(patched / 'src/crc32')],
            sources=[source / 'support/beebsc.c'])

    def test_skl_copy_spans_domains_and_modes(self):
        source = self.source('RHODIUM_SKL_SOURCE', 'sw/skl', 'test/skl-test-driver.h')
        patched = materialize(source, ROOT / 'sw/skl-patches/series', self.work / 'source')
        self.compile_run(r'''
#include <assert.h>
#include "buffers.h"
static int fail_allocation;
void *skl_test_driver_alloc(size_t region, size_t bytes) {
  assert(region == 7);
  if (fail_allocation) return NULL;
  unsigned char *p = malloc(bytes + 8);
  memset(p, 0xa5, bytes + 8);
  return p;
}
void skl_test_driver_free(size_t region, void *p) { assert(region == 7); free(p); }
void skl_test_driver_log(skl_test_t *t, FILE *stream, const char *fmt, ...) {}
int main(void) {
  skl_test_t t = {0};
  t.id = 31;
  size_t lengths[] = {0, 1, 1023, 1024, 1025, 4099};
  for (size_t k = 0; k < 6; ++k) {
    SKL_TEST_BUFFER(uint32_t) b = {.region = 7, .len = lengths[k]};
    RHODIUM_SKL_SOURCE(&t, uint32_t, &b);
    for (size_t i = 0; i < b.len; ++i)
      assert(b.data[i] == rhodium_skl_uint32_t[(31 * 131 + k * 317 + i) % 1024]);
    for (int i = 0; i < 8; ++i)
      assert(((unsigned char *)b.data)[b.len * sizeof(uint32_t) + i] == 0xa5);
    SKL_TEST_BUF_FREE(&t, &b);
  }
  SKL_TEST_BUFFER(int32_t) c = {.region = 7, .len = 2051, .min = -128,
                               .max = 127, .mode = SKL_TEST_RANDOM};
  RHODIUM_SKL_INTEGER_SOURCE(&t, int32_t, &c);
  for (size_t i = 0; i < c.len; ++i) assert(c.data[i] >= -128 && c.data[i] <= 127);
  SKL_TEST_BUF_FREE(&t, &c);
  const int8_t values[] = {-3, 1, 8};
  SKL_TEST_BUFFER(int8_t) b = {.region = 7, .len = 7, .mode = SKL_TEST_STATIC,
                             .static_data = values, .static_data_len = 3};
  RHODIUM_SKL_INTEGER_SOURCE(&t, int8_t, &b);
  for (size_t i = 0; i < b.len; ++i) assert(b.data[i] == values[i % 3]);
  SKL_TEST_BUF_FREE(&t, &b);
  b.mode = SKL_TEST_SEQ; b.min = -7; b.max = 7;
  RHODIUM_SKL_INTEGER_SOURCE(&t, int8_t, &b);
  for (size_t i = 0; i < b.len; ++i) assert(b.data[i] == -7 + 2 * (int)i);
  SKL_TEST_BUF_FREE(&t, &b);
  b.data = NULL; b.mode = SKL_TEST_RANDOM; b.min = -1; b.max = 1;
  RHODIUM_SKL_INTEGER_SOURCE(&t, int8_t, &b);
  assert(t.status.init_status == SKL_TEST_FAIL && b.data == NULL);
  t.status.init_status = SKL_TEST_PASS; fail_allocation = 1;
  assert(rhodium_skl_allocate(&t, 7, 8) == NULL);
  assert(t.status.init_status == SKL_TEST_FAIL);
}
''', flags=['-I' + str(patched / 'test')])


if __name__ == '__main__':
    unittest.main()
