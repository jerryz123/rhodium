#!/usr/bin/env python3
# Checks fully compiled operations, static ownership, and double-buffered RTL state.
import pathlib
import sys
import struct
import random
import json
import re
import ctypes as C

from runtime_test import (Native, Options, library, check_arithmetic, check_hierarchy,
                          check_storage, check_aggregate, check_assertions, check_pipeline)


def check_onehot_words(lib, directory):
    # Exercise the selected payload, including bit 63 and all pointer banks;
    # zero/multi-hot diagnostics and the >64-bit fallback remain observable.
    rng = random.Random(8495)
    for selector_width, width in [(1, 1), (7, 65), (64, 244), (65, 65)]:
        words, mask = (width + 63) // 64, (1 << width) - 1
        constants = [rng.getrandbits(width) for _ in range(3)]
        for bank in ('input', 'state', 'constant', 'mixed'):
            lanes = [(1 if bank == 'input' else 4 if bank == 'state' else 7) + i % 3
                     if bank != 'mixed' else 1 + i % 9 for i in range(selector_width)]
            path = directory / f'onehot-{selector_width}-{bank}.rsim'
            ports = [(0, 0, 'selector'), *[(0, i + 1, f'data{i}') for i in range(3)], (1, 10, 'result')]
            image = bytearray(b'RHDMSIM\0')
            image += struct.pack('<12I', 2, 11, 4, selector_width + 1, 3 * words, 3, 0, 0, 0, 0, len(ports), 0)
            image += struct.pack('<11I', selector_width, *([width] * 10))
            for i in range(3):
                image += struct.pack('<6I', 0, 7 + i, 0, 0, i * words, words)
            image += struct.pack('<6I', 16, 10, 0, selector_width + 1, 3 * words, 0)
            image += struct.pack(f'<{selector_width + 1}I', 0, *lanes)
            for value in constants:
                image += struct.pack(f'<{words}Q', *[(value >> (64 * j)) & ((1 << 64) - 1) for j in range(words)])
            for i in range(3):
                image += struct.pack('<4I', 4 + i, 1 + i, 0xFFFFFFFF, 0xFFFFFFFF)
            for direction, value, name in ports:
                image += struct.pack('<3I', direction, value, len(name)) + name.encode()
            path.write_bytes(image)
            for release in (False, True):
                lib.options, lib.shape_limit, lib.compiled, lib.release = Options(1, 331792), 512, True, release
                sim = Native(lib, path)
                previous = [0] * 3
                for _ in range(3):
                    data = [rng.getrandbits(width) for _ in range(3)]
                    for i, value in enumerate(data):
                        sim.set(f'data{i}', value)
                    values = [0, *data, *previous, *constants]
                    for lane in range(selector_width):
                        sim.set('selector', 1 << lane)
                        sim.eval()
                        assert sim.get('result') == values[lanes[lane]] & mask
                    for invalid in (0, *([3, (1 << selector_width) - 1] if selector_width > 1 else [])):
                        sim.set('selector', invalid)
                        lib.rds_set_strict(sim.ptr, 1)
                        assert lib.rds_eval(sim.ptr) != 0
                        assert (b'multi-hot' if invalid else b'zero-hot') in lib.rds_error(sim.ptr)
                        lib.rds_set_strict(sim.ptr, 0)
                        sim.eval()
                        assert sim.get('result') == 0
                    sim.set('selector', 1)
                    lib.rds_set_strict(sim.ptr, 1)
                    sim.eval()
                    sim.advance()
                    previous = data
                sim.close()
    lib.release = False
    print('One-hot selection: high bits, pointer banks, payloads, bank swaps and invalid selectors passed')


def check_onehot_objects(lib, directory):
    # Compare address-tree leaves and table fallbacks with materializing objects.
    # Twelve alternatives cover every level of a non-power-of-two balanced tree.
    rng = random.Random(61148)
    for kind, depth, count in [(k, d, n) for k, d in [(1, 1), (1, 3), (2, 3), (3, 1), (9, 3)] for n in (2, 12)]:
        width = 244
        path = directory / f'onehot-object-{kind}-{depth}-{count}.rsim'
        names = ['reset'] + [f'{field}_{i}' for i in range(count) for field in ('valid', 'data', 'ready')] + ['selector']
        widths = [1] + [w for _ in range(count) for w in (1, width, 1)] + [count] + [width] * (count + 1)
        first_query, result = len(names), len(widths) - 1
        image = bytearray(b'RHDMSIM\0')
        image += struct.pack('<12I', 2, len(widths), count + 1, count + 1, count * 2, 0, 0, 0, 0, 0, len(names) + 1, count)
        image += struct.pack(f'<{len(widths)}I', *widths)
        for i in range(count):
            image += struct.pack('<6I', 29, first_query + i, 0, 0, 2 * i, 2)
        image += struct.pack('<6I', 16, result, 0, count + 1, count * 2, 0)
        image += struct.pack(f'<{count + 1}I', first_query - 1, *range(first_query, result))
        image += struct.pack(f'<{count * 2}Q', *[x for i in range(count) for x in (i, 1 if kind == 9 else 3)])
        for value, name in enumerate(names):
            image += struct.pack('<3I', 0, value, len(name)) + name.encode()
        image += struct.pack('<3I', 1, result, 6) + b'result'
        for i in range(count):
            inputs = [0, 1 + 3 * i, 2 + 3 * i] + [3 + 3 * i] * (depth if kind == 9 else 1)
            name = f'object{i}'.encode()
            image += struct.pack('<5I', kind, width, depth, 0, len(inputs))
            image += struct.pack(f'<{len(inputs)}I', *inputs)
            image += struct.pack('<I', len(name)) + name
        path.write_bytes(image)
        sims = []
        for compiled, workers, flags in [(False, 1, 0), (True, 1, 331792), (True, 4, 348752)]:
            lib.compiled, lib.options, lib.shape_limit = compiled, Options(workers, flags), 512
            sims.append(Native(lib, path))
        for cycle in range(60):
            data = [int(cycle % 29 == 0)] + [x for _ in range(count) for x in
                    (rng.randrange(2), rng.getrandbits(width), rng.randrange(2))]
            for sim in sims:
                for name, value in zip(names, data):
                    sim.set(name, value)
            for selector in (0, *[1 << i for i in range(count)], 3):
                values = []
                for sim in sims:
                    sim.set('selector', selector)
                    sim.eval()
                    values.append(sim.get('result'))
                assert len(set(values)) == 1, (kind, depth, count, cycle, selector, values)
            for sim in sims:
                sim.advance()
        for sim in sims:
            sim.close()
    lib.compiled = True
    print('One-hot object payloads: every tree leaf, snapshots, stalls, reset, publication and parallel plans passed')


def check_functional_updates(lib, directory):
    rng = random.Random(209731)
    for length, iw, ew in [(1, 1, 1), (3, 2, 4), (32, 5, 1), (32, 64, 1), (3, 2, 65)]:
        stem = f"functional-update-{length}-{iw}-{ew}"
        graph = json.loads((directory / (stem + ".json")).read_text())
        assert sum(op[0] == 23 for op in graph["operations"]) == 1
        assert len(graph["operations"]) <= 7
        cases = [(rng.getrandbits(length * ew), index, rng.getrandbits(ew))
                 for index in list(range(min(1 << iw, length + 2))) + [(1 << iw) - 1]
                 for _ in range(4)]
        for compiled, release in [(False, False), (True, False), (True, True)]:
            lib.options, lib.shape_limit = Options(1, 0), 512
            lib.compiled, lib.release = compiled, release
            sim = Native(lib, directory / (stem + ".rsim"))
            lib.rds_set_strict(sim.ptr, 1)
            for source, index, data in cases:
                sim.set("source", source)
                sim.set("index", index)
                sim.set("data", data)
                sim.eval()
                expected = source
                if index < length:
                    mask = ((1 << ew) - 1) << (index * ew)
                    expected = (source & ~mask) | (data << (index * ew))
                assert sim.get("result") == expected, (stem, compiled, release, index)
            sim.close()
    lib.release = False


def check_cones(lib, directory):
    rng = random.Random(5029)
    for width in (32, 64):
        word_mask = (1 << width) - 1
        model = json.loads((directory / f"word-cones-{width}.json").read_text())
        assert any(op[0] == 31 for op in model["operations"])
        definitions = {op[1]: op for op in model["operations"]}
        output_ids = {p[2]: p[1] for p in model["ports"]}
        # The redundant outer guard must actually disappear, not just evaluate correctly.
        nested = definitions[output_ids["nested"]]
        assert nested[0] == 15 and all(definitions.get(a, [None])[0] != 15 for a in nested[2][1:])
        image = bytearray((directory / f"word-cones-{width}.rsim").read_bytes())
        _, nv, no, na, *_ = struct.unpack_from("<12I", image, 8)
        op_start = 56 + 4 * nv
        merge = next(i for i in range(no) if struct.unpack_from("<I", image, op_start + 24 * i)[0] == 31)
        bad = bytearray(image)
        struct.pack_into("<I", bad, op_start + 24 * merge + 12, 2)
        path = directory / "invalid-byte-merge.rsim"
        path.write_bytes(bad)
        error = C.create_string_buffer(512)
        assert not lib.rds_load(str(path).encode(), error, len(error))
        for flags, limit in ((0, 512), (16, 1), (256, 512), (65536, 512), (65552, 1), (65792, 512), (98304, 512), (4096, 512), (69632, 512)):
            lib.options, lib.shape_limit, lib.compiled = Options(1, flags), limit, True
            sim = Native(lib, directory / f"word-cones-{width}.rsim")
            lib.options, lib.compiled = Options(1, 1), False
            raw = Native(lib, directory / f"word-cones-{width}.rsim")
            state = 0
            for cycle in range(1024):
                old, incoming, local = [rng.getrandbits(width) for _ in range(3)]
                enabled, pending = (cycle >> 8) & 1, (cycle >> 9) & 1
                mask = cycle & ((1 << (width // 8)) - 1)
                reset = int(cycle in (0, 33, 66, 507))
                byte_mask = sum(255 << (i * 8) for i in range(width // 8) if mask >> i & 1)
                result = local if pending else (old & ~byte_mask | incoming & byte_mask) if enabled else old
                for x in (sim, raw):
                    for name, value in dict(reset=reset, old=old, incoming=incoming, local=local, mask=mask,
                                            enabled=enabled, pending=pending, okay=1).items():
                        x.set(name, value)
                    x.eval()
                    assert x.get("result") == result
                    assert x.get("roundtrip") == old
                    assert x.get("nested") == (old if enabled else incoming)
                    assert x.get("absorbed") == pending
                    assert x.get("state_out") == state
                    if not reset and cycle % 127 == 126:
                        x.set("okay", 0); x.eval()
                        assert lib.rds_advance(x.ptr) != 0
                        assert x.get("state_out") == state
                        x.set("okay", 1); x.eval()
                    x.advance()
                state = 7 if reset else result | (state & word_mask) << width
            source = lib.compiled_cache[(directory / f"word-cones-{width}.rsim", 1, flags, limit)].with_suffix('.c').read_text()
            assert f"direct register packs: {1 if flags & 65536 else 0}" in source
            sim.close(); raw.close()
    for flow in (0, 2, 4):
        for late in (False, True):
            name = f"demand-fifo-{flow}" + ("-late" if late else "")
            for workers, flags in ((1, 0), (1, 16), (1, 32768), (4, 64)):
                lib.options, lib.shape_limit, lib.compiled = Options(workers, flags), 512, True
                sim = Native(lib, directory / f"{name}.rsim")
                lib.options, lib.compiled = Options(1, 1), False
                raw = Native(lib, directory / f"{name}.rsim")
                for cycle in range(160):
                    stimulus = dict(reset=int(cycle in (0, 50)), valid=int(cycle % 7 != 0), ready=int(cycle % 11 > 6),
                                    data=rng.getrandbits(64), vector=rng.getrandbits(192), index=cycle % 4)
                    for x in (sim, raw):
                        for key, value in stimulus.items(): x.set(key, value)
                        x.eval()
                    for key in ("accept", "payload", "available", "shared"):
                        assert sim.get(key) == raw.get(key), (name, cycle, key)
                    for x in (sim, raw): x.advance()
                # Even when there is no enqueue, invalid partial operators fail.
                for x in (sim, raw):
                    x.set("valid", 0); x.set("index", 3)
                    lib.rds_set_strict(x.ptr, 1)
                    assert lib.rds_eval(x.ptr) != 0
                    assert b"vector index" in lib.rds_error(x.ptr)
                    x.set("index", 2); x.eval()
                source = lib.compiled_cache[(directory / f"{name}.rsim", workers, flags, 512)].with_suffix('.c').read_text()
                gated = int(re.search(r"demand-gated operations: (\d+)", source)[1])
                if flow == 4 or flags & 32768: assert gated == 0
                elif not late and workers == 1: assert gated >= 2
                sim.close(); raw.close()
    print("Word recovery, guard simplification, direct state sinks, and demand-gated FIFO cones passed", flush=True)


def check_object_attachment(lib, directory):
    # Exercise hot state with tracing disabled during initial and repeated attachment.
    trace_type=C.CFUNCTYPE(None,C.c_void_p,C.c_uint64,C.c_char_p,C.c_uint32,C.c_uint32,C.c_int,C.c_int)
    lib.rds_set_object_trace.argtypes=[C.c_void_p,trace_type,C.c_void_p]
    for name in ('fifo-8-3-00','broadcast'):
        for workers in (1,4):
            lib.options,lib.shape_limit,lib.compiled=Options(workers,65552),512,True
            template=Native(lib,directory/f'{name}.rsim')
            binary=lib.compiled_cache[(directory/f'{name}.rsim',workers,65552,512)]
            template.close();lib.compiled=False
            sim=Native(lib,directory/f'{name}.rsim');reference=Native(lib,directory/f'{name}.rsim')
            traces=[[],[]]
            callbacks=[trace_type(lambda _,cycle,path,kind,count,push,pop,slot=slot: traces[slot].append((cycle,path,kind,count,push,pop))) for slot in (0,1)]
            for cycle in range(100):
                if cycle in (17,18,33,57):sim.check(lib.rds_use_compiled(sim.ptr,str(binary).encode()))
                if cycle==45:
                    for model,callback in zip((sim,reference),callbacks):lib.rds_set_object_trace(model.ptr,callback,None)
                stimulus=dict(reset=int(cycle in (0,70)),ingress_in=((cycle%5!=0)<<(8 if name.startswith('fifo') else 65))|cycle)
                outputs=['count','ingress_out','egress_out'] if name.startswith('fifo') else ['ingress_out',*[f'egress_{i}_out' for i in range(3)]]
                if name.startswith('fifo'):stimulus['egress_in']=int(cycle%7>3)
                else:stimulus.update({f'egress_{i}_in':int((cycle+i)%7>3) for i in range(3)})
                for model in (sim,reference):
                    for key,value in stimulus.items():model.set(key,value)
                    model.eval()
                for key in outputs:assert sim.get(key)==reference.get(key),(name,workers,cycle,key)
                sim.advance();reference.advance()
                assert traces[0]==traces[1],(name,workers,cycle,traces)
            sim.close();reference.close()
    # Host binding is mutable cold metadata; compiled callbacks must see rebinding.
    import os,subprocess
    from semantic_test import BASE
    model=dict(format='rhodium-simulation-ir-v1',opcodes=BASE,values=[1,1,1,32,1,64],
               operations=[[29,5,[],[0,0]]],ports=[[0,i,n] for i,n in enumerate(('reset','ready','valid','data','start'))]+[[1,5,'result']],
               registers=[],memories=[],writes=[],reads=[],assertions=[],objects=[[8,0,8,0,[0,1,2,3,4],'host']],
               origins=[],inventory=[],occurrences=[],replicated_bytes=0,replicated_work=0)
    source=directory/'attachment-host.json';source.write_text(json.dumps(model));binary=source.with_suffix('.rsim')
    compiler=os.getenv('RDS_OPTIMIZER',str(pathlib.Path(__file__).resolve().parents[2]/'rhodium/sim/compiler/run.sh'))
    subprocess.run([compiler,'--input',str(source),'--output',str(binary),'--no-optimize'],check=True)
    host_type=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(C.c_uint64),C.c_size_t,C.POINTER(C.c_uint64),C.c_size_t)
    calls=[]
    @host_type
    def host(tag,inputs,ni,outputs,no):
        calls.append((tag,inputs[3]));outputs[0]=tag+inputs[3]
        return int(tag==200 and inputs[3]==12)
    lib.rds_bind_host.argtypes=[C.c_void_p,C.c_char_p,host_type,C.c_void_p]
    lib.options,lib.shape_limit,lib.compiled=Options(1,65552),512,True
    sim=Native(lib,binary);expected=0;tag=100
    sim.check(lib.rds_bind_host(sim.ptr,None,host,C.c_void_p(tag)))
    for cycle in range(20):
        if cycle==7:tag=200;sim.check(lib.rds_bind_host(sim.ptr,None,host,C.c_void_p(tag)))
        sim.set('data',cycle);sim.eval();assert sim.get('result')==expected
        if cycle==12:
            assert lib.rds_advance(sim.ptr)!=0
            sim.eval();assert sim.get('result')==expected
            tag=300;sim.check(lib.rds_bind_host(sim.ptr,None,host,C.c_void_p(tag)))
        sim.advance();expected=tag+cycle
    assert len(calls)==21
    sim.close();lib.compiled=True
    print('Object attachment, trace counts, mutable host binding and callback failure recovery passed',flush=True)


def main():
    directory = pathlib.Path(sys.argv[1])
    lib = library(directory / "librhodium_sim.so")
    lib.compiled, lib.compiled_cache = True, {}
    for workers, flags, limit in [(1, 0, 512), (4, 0, 512), (4, 64, 512), (1, 16, 1), (4, 192, 512), (1, 256, 512), (1, 2048, 512), (1, 4096, 512), (4, 16960, 512), (1, 131072, 512), (4, 131072, 512), (1, 65552, 1), (4, 65536, 512), (1, 331792, 512), (4, 348752, 512)]:
        lib.options, lib.shape_limit = Options(workers, flags), limit
        print(f"compiled: workers={workers}, flags={flags}, shape budget={limit}", flush=True)
        for check in (check_arithmetic, check_hierarchy, check_storage, check_aggregate,
                      check_assertions, check_pipeline):
            check(lib, directory)
    check_functional_updates(lib, directory)
    check_onehot_words(lib, directory)
    check_onehot_objects(lib, directory)
    check_cones(lib, directory)
    check_object_attachment(lib, directory)
    # Attaching after execution must preserve current state; replacing code on
    # either bank parity must neither reset state nor alter hold/reset behavior.
    for flags in (0, 128):
        lib.options, lib.shape_limit, lib.compiled = Options(1, flags), 512, True
        template = Native(lib, directory / "hierarchy.rsim")
        binary = lib.compiled_cache[(directory / "hierarchy.rsim", 1, flags, 512)]
        template.close()
        lib.compiled = False
        sim = Native(lib, directory / "hierarchy.rsim")
        first = second = 0
        for cycle in range(80):
            if cycle in (17, 18, 23, 24):
                sim.check(lib.rds_use_compiled(sim.ptr, str(binary).encode()))
            reset, amount = int(cycle in (31, 42)), 0 if cycle % 3 else cycle * 91
            sim.set("rst", reset)
            sim.set("amount", amount)
            sim.eval()
            assert (sim.get("first_value"), sim.get("second_value")) == (first, second)
            sim.advance()
            first, second = (0, 0) if reset else ((first + amount) & 0xFFFFFFFF, (second + first) & 0xFFFFFFFF)
        sim.close()
    lib.compiled = True
    lib.options, lib.shape_limit = Options(1, 0), 512
    first = Native(lib, directory / "arithmetic-7.rsim")
    second = Native(lib, directory / "arithmetic-31.rsim")
    wrong = lib.compiled_cache[(directory / "arithmetic-7.rsim", 1, 0, 512)]
    assert lib.rds_use_compiled(second.ptr, str(wrong).encode()) != 0
    assert b"does not match" in lib.rds_error(second.ptr)
    assert lib.rds_emit_c(first.ptr, str(directory / "invalid.c").encode(), 4097) != 0
    image = bytearray((directory / "arithmetic-7.rsim").read_bytes())
    _, nv, no, na, *_ = struct.unpack_from("<12I", image, 8)
    op_start, immediate_start = 56 + 4 * nv, 56 + 4 * nv + 24 * no + 4 * na
    for index in range(no):
        code, _, _, _, immediate, _ = struct.unpack_from("<6I", image, op_start + 24 * index)
        if code == 0:
            value = struct.unpack_from("<Q", image, immediate_start + 8 * immediate)[0]
            struct.pack_into("<Q", image, immediate_start + 8 * immediate, value ^ 1)
            break
    else:
        raise AssertionError("constant fixture is required")
    altered = directory / "altered-constant.rsim"
    altered.write_bytes(image)
    lib.compiled = False
    changed = Native(lib, altered)
    assert lib.rds_use_compiled(changed.ptr, str(wrong).encode()) != 0
    assert b"does not match" in lib.rds_error(changed.ptr)
    changed.close()
    # Rejecting an attachment leaves the previous correct executable usable.
    second.set("a", 31)
    second.set("b", 9)
    second.eval()
    assert second.get("add") == 40
    first.close()
    second.close()
    print("compiled plan identity, direct-call budget, bank parity, and failure recovery passed")


if __name__ == "__main__":
    main()
