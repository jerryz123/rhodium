#!/usr/bin/env python3
# Compares the native runtime with independent arithmetic and transition models.
import ctypes as C
import pathlib
import os
import random
import struct
import sys
import subprocess
import shlex

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "rhodium/sim/runtime"


class Options(C.Structure):
    _fields_ = [("workers", C.c_uint32), ("flags", C.c_uint32)]


class Stats(C.Structure):
    _fields_ = [(name, C.c_size_t) for name in ("model_bytes", "value_bytes", "memory_bytes", "update_bytes")]
    _fields_ += [(name, C.c_uint32) for name in ("values", "operations", "registers", "memories", "ports")]
    _fields_ += [("cycles", C.c_uint64), ("descriptor_bytes", C.c_size_t), ("schedule_bytes", C.c_size_t)]
    _fields_ += [(name, C.c_uint32) for name in ("workers", "components", "batches")]
    _fields_ += [("objects", C.c_uint32), ("object_bytes", C.c_size_t), ("payload_bytes", C.c_size_t)]


class Native:
    def __init__(self, lib, path):
        self.lib = lib
        if lib.options.flags & 1:
            path = path.with_stem(path.stem + "-raw")
        error = C.create_string_buffer(512)
        self.ptr = lib.rds_load_with_options(str(path).encode(), C.byref(lib.options), error, len(error))
        assert self.ptr, error.value.decode()
        if getattr(lib, "compiled", False) and not lib.options.flags & 1:
            key = (path, lib.options.workers, lib.options.flags, lib.shape_limit)
            if getattr(lib, "release", False):
                key += ("release",)
            if key not in lib.compiled_cache:
                variant = "-release" if getattr(lib, "release", False) else ""
                source = path.with_suffix(f".{lib.options.workers}-{lib.options.flags}-{lib.shape_limit}{variant}.c")
                self.check(lib.rds_emit_c(self.ptr, str(source).encode(), lib.shape_limit))
                binary = source.with_suffix(".so")
                subprocess.run([os.getenv("CC", "cc"), "-std=c17", "-O2", "-fPIC", "-shared",
                                "-Wall", "-Wextra", "-Werror", *(["-DNDEBUG"] if getattr(lib, "release", False) else []), *shlex.split(os.getenv("RDS_SANITIZER_FLAGS", "")),
                                str(source), "-o", str(binary)], check=True)
                lib.compiled_cache[key] = binary
            self.check(lib.rds_use_compiled(self.ptr, str(lib.compiled_cache[key]).encode()))

    def close(self):
        self.lib.rds_free(self.ptr)
        self.ptr = None

    def port(self, name):
        p = self.lib.rds_find_port(self.ptr, name.encode())
        assert p >= 0, name
        return p

    def check(self, status):
        assert status == 0, self.lib.rds_error(self.ptr).decode()

    def set(self, name, value):
        p = self.port(name)
        n = (self.lib.rds_port_width(self.ptr, p) + 63) // 64
        words = (C.c_uint64 * n)(*(value >> (64 * i) & ((1 << 64) - 1) for i in range(n)))
        self.check(self.lib.rds_set(self.ptr, p, words, n))

    def get(self, name):
        p = self.port(name)
        n = (self.lib.rds_port_width(self.ptr, p) + 63) // 64
        words = (C.c_uint64 * n)()
        self.check(self.lib.rds_get(self.ptr, p, words, n))
        return sum(int(v) << (64 * i) for i, v in enumerate(words))

    def eval(self):
        self.check(self.lib.rds_eval(self.ptr))

    def advance(self):
        self.check(self.lib.rds_advance(self.ptr))


def library(path):
    lib = C.CDLL(str(path))
    lib.rds_load.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t]
    lib.rds_load.restype = C.c_void_p
    lib.rds_load_with_options.argtypes = [C.c_char_p, C.POINTER(Options), C.c_char_p, C.c_size_t]
    lib.rds_load_with_options.restype = C.c_void_p
    lib.rds_get_stats.argtypes = [C.c_void_p]
    lib.rds_get_stats.restype = Stats
    lib.rds_set_strict.argtypes = [C.c_void_p, C.c_int]
    lib.rds_free.argtypes = [C.c_void_p]
    lib.rds_error.argtypes = [C.c_void_p]
    lib.rds_error.restype = C.c_char_p
    lib.rds_find_port.argtypes = [C.c_void_p, C.c_char_p]
    lib.rds_port_width.argtypes = [C.c_void_p, C.c_uint32]
    for name in ("rds_set", "rds_get"):
        getattr(lib, name).argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint64), C.c_size_t]
    for name in ("rds_eval", "rds_advance"):
        getattr(lib, name).argtypes = [C.c_void_p]
    lib.rds_emit_c.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32]
    lib.rds_emit_plan.argtypes = [C.c_void_p, C.c_char_p]
    lib.rds_use_compiled.argtypes = [C.c_void_p, C.c_char_p]
    return lib


def check_arithmetic(lib, directory):
    rng = random.Random(20260907)
    trials = 0
    for width in (1, 7, 31, 32, 33, 63, 64, 65, 127, 128, 129):
        sim = Native(lib, directory / f"arithmetic-{width}.rsim")
        mask = (1 << width) - 1
        for i in range(300):
            a, b = (rng.getrandbits(width) for _ in range(2))
            if i < 3:
                a = [0, mask, mask - 1][i]
            shift = rng.choice([0, 1, width - 1, width, width + 1, 255])
            sim.set("a", a)
            sim.set("b", b)
            sim.set("shift", shift)
            increment, decrement = rng.randrange(2), rng.randrange(2)
            sim.set("increment", increment)
            sim.set("decrement", decrement)
            sim.eval()
            sa = a - (1 << width) if a >> (width - 1) else a
            sb = b - (1 << width) if b >> (width - 1) else b
            expected = dict(add=(a + b) & mask, sub=(a - b) & mask,
                            mul=(a * b) & mask, xor=a ^ b, shl=(a << shift) & mask,
                            shru=a >> shift, eq=int(a == b), ult=int(a < b),
                            slt=int(sa < sb), shrs=(sa >> shift) & mask,
                            sext=sa & ((1 << (width + 17)) - 1), zext=a,
                            balance=(a + increment - decrement) & mask,
                            set_clear=(a | b) & ~b & mask,
                            counter_step=a if not increment else 0 if a == mask - 1 else (a + 1) & mask)
            expected.update({"and": a & b, "or": a | b, "not": (~a) & mask})
            for name, value in expected.items():
                assert sim.get(name) == value, (width, i, name, a, b, shift)
                trials += 1
        sim.close()
    print(f"arithmetic: {trials} independent observations passed")


def check_hierarchy(lib, directory):
    sim = Native(lib, directory / "hierarchy.rsim")
    first = second = 0
    for cycle in range(500):
        reset = int(cycle in (0, 1, 113, 299))
        amount = (cycle * 0xBADCAF) & 0xFFFFFFFF
        sim.set("rst", reset)
        sim.set("amount", amount)
        sim.eval()
        assert sim.get("first_value") == first
        assert sim.get("second_value") == second
        sim.advance()
        first, second = (0, 0) if reset else ((first + amount) & 0xFFFFFFFF, (second + first) & 0xFFFFFFFF)
        out = C.c_uint64()
        assert sim.lib.rds_get(sim.ptr, sim.port("first_value"), C.byref(out), 1) != 0
    sim.close()
    print("hierarchy: independent occurrences, old-state reads, reset, and observation phases passed")


def check_storage(lib, directory):
    sim = Native(lib, directory / "storage.rsim")
    asynchronous = [0] * 4
    synchronous = [0] * 4
    response = 0
    rng = random.Random(671)
    for cycle in range(600):
        addr, data, mask = rng.randrange(4), rng.getrandbits(64), rng.getrandbits(8)
        enable, write = rng.randrange(2), rng.randrange(2)
        for name, value in dict(addr=addr, data=data, mask=mask, enable=enable, write=write).items():
            sim.set(name, value)
        sim.eval()
        assert sim.get("async_data") == asynchronous[addr]
        assert sim.get("sync_data") == response
        if enable:
            if write:
                asynchronous[addr] = data
                bits = sum(255 << (8 * i) for i in range(8) if mask >> i & 1)
                synchronous[addr] = (synchronous[addr] & ~bits) | (data & bits)
            else:
                response = synchronous[addr]
        sim.advance()
        sim.eval()
        assert sim.get("async_data") == asynchronous[addr]
        assert sim.get("sync_data") == response
    sim.close()
    print("storage: old reads, deferred writes, synchronous latency, and byte masks passed")


def check_loader(lib, directory):
    image = (directory / "hierarchy.rsim").read_bytes()
    error = C.create_string_buffer(512)
    path = directory / "malformed.rsim"
    for n in (0, 7, 12, 51, len(image) - 1):
        path.write_bytes(image[:n])
        assert not lib.rds_load(str(path).encode(), error, len(error))
    bad = bytearray(image)
    struct.pack_into("<I", bad, 12, 0xFFFFFFFF)
    path.write_bytes(bad)
    assert not lib.rds_load(str(path).encode(), error, len(error))
    print("loader: truncated images and impossible counts rejected")


def check_aggregate(lib, directory, stem="aggregate"):
    sim = Native(lib, directory / (stem + ".rsim"))
    rng = random.Random(391)
    for _ in range(300):
        a, b = rng.getrandbits(65), rng.getrandbits(65)
        index = rng.randrange(3)
        other_index = (index + 1) % 3
        enables = rng.randrange(4)
        lane = rng.randrange(3)
        selector = rng.choice([0, 1 << 64, (1 << 64) + 1])
        for name, val in dict(a=a, b=b, index=index, other_index=other_index,
                              enables=enables, onehot=1 << lane, selector=selector).items():
            sim.set(name, val)
        sim.eval()
        first_updated = b if index & 1 == 0 else a
        assert sim.get("updated_leaf") == first_updated
        assert sim.get("updated_vector") == first_updated | ((b if index & 1 else first_updated) << 65)
        vector = [a, b, 7]
        pack = lambda elements: sum(v << (65 * i) for i, v in enumerate(elements))
        injected = vector.copy()
        injected[index] = b
        written = vector.copy()
        if enables & 1:
            written[index] = b
        if enables & 2:
            written[other_index] = a
        record = (a << 65) | b
        for low in (0, 1, 31, 63, 64, 65, 66):
            for width in (1, 7, 31, 63, 64):
                assert sim.get(f"slice_{low}_{width}") == (record >> low) & ((1 << width) - 1)
        expected = dict(record=record, cross_word=(record >> 31) & ((1 << 66) - 1),
                        vector=pack(vector), indexed=vector[index], injected=pack(injected),
                        written=pack(written), onehot_value=vector[lane],
                        lookup=b if selector == 1 << 64 else a,
                        decoded=5 if selector >> 64 else 2, leaf_cycle=(a << 65) | a)
        for name, val in expected.items():
            assert sim.get(name) == val, (name, val, sim.get(name))
    sim.set("index", 3)
    sim.set("enables", 0)
    sim.eval()
    assert sim.get("indexed") == 0
    lib.rds_set_strict(sim.ptr, 1)
    assert lib.rds_eval(sim.ptr) != 0
    assert b"vector index out of range" in lib.rds_error(sim.ptr)
    sim.set("index", 0)
    sim.set("onehot", 0)
    assert lib.rds_eval(sim.ptr) != 0
    assert b"onehot_mux" in lib.rds_error(sim.ptr)
    sim.set("onehot", 1)
    sim.set("other_index", 0)
    sim.set("enables", 3)
    assert lib.rds_eval(sim.ptr) != 0
    assert b"vector write collision" in lib.rds_error(sim.ptr)
    sim.close()
    print("aggregates: leaf-sensitive cycles, packing, wide lookup, decode, and vector writes passed")


def check_assertions(lib, directory):
    sim = Native(lib, directory / "checked.rsim")
    sim.set("okay", 0)
    sim.set("guard", 1)
    sim.set("rst", 1)
    sim.advance()  # Synchronous reset disables the assertion.
    sim.set("rst", 0)
    sim.eval()
    assert sim.get("value") == 0
    assert lib.rds_advance(sim.ptr) != 0
    assert b"required_condition" in lib.rds_error(sim.ptr)
    sim.eval()
    assert sim.get("value") == 0  # Failed transitions publish no state.
    sim.set("guard", 0)
    sim.advance()
    sim.eval()
    assert sim.get("value") == 1
    sim.close()
    print("assertions: clock, guard, reset, and atomic failure passed")


def check_pipeline(lib, directory):
    sim = Native(lib, directory / "pipeline.rsim")
    state = [0] * 8
    for cycle in range(30):
        reset, amount = int(cycle in (0, 17)), cycle * 13397
        sim.set("rst", reset)
        sim.set("amount", amount)
        sim.eval()
        assert [sim.get(f"lane_{i}") for i in range(8)] == state
        if reset:
            state = list(range(8))
        else:
            for lane in range(8):
                value = state[lane] ^ amount
                for _ in range(256):
                    value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
                    value ^= value >> 13
                state[lane] = value
        sim.advance()
    stats = lib.rds_get_stats(sim.ptr)
    if not lib.options.flags & 1:
        assert stats.value_bytes < 1024, stats.value_bytes
        assert stats.workers == lib.options.workers
    print(f"pipeline: snapshot recurrence passed; {stats.value_bytes} value bytes, {stats.workers} workers")
    sim.close()


def main():
    directory = pathlib.Path(sys.argv[1])
    lib = library(directory / "librhodium_sim.so")
    modes = [("reference", 1, 1), ("batched", 1, 0),
             ("reused generic", 1, 4), ("parallel", 4, 0),
             ("parallel epochs", 4, 64), ("component epochs", 4, 96), ("coalesced scratch", 1, 131072),
             ("coalesced parallel", 4, 131072)]
    if os.getenv("RDS_TEST_ASM"):
        modes.append(("assembly", 1, 8))
    for label, workers, flags in modes:
        print(f"execution mode: {label}")
        lib.options = Options(workers, flags)
        check_arithmetic(lib, directory)
        check_hierarchy(lib, directory)
        check_storage(lib, directory)
        check_aggregate(lib, directory)
        check_assertions(lib, directory)
        check_pipeline(lib, directory)
    check_loader(lib, directory)


if __name__ == "__main__":
    main()
