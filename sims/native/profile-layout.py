#!/usr/bin/env python3
# Attributes AMD IBS load samples to native simulator allocations and generated data.
import argparse
import collections
import ctypes
import fcntl
import hashlib
import json
import mmap
import os
from pathlib import Path
import select
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SAMPLE_FIELDS = 1 | 2 | 8 | (1 << 14) | (1 << 15)  # IP, TID, address, weight, source


def decode_records(data):
    """Decode completed perf records; retain precise user-space addressed loads."""
    samples, lost, pos = [], 0, 0
    while pos < len(data):
        if len(data) - pos < 8:
            raise ValueError("truncated perf header")
        kind, misc, size = struct.unpack_from("IHH", data, pos)
        if size < 8 or pos + size > len(data):
            raise ValueError("invalid perf record size")
        if kind == 9:
            if size != 48:
                raise ValueError("unexpected IBS sample format")
            ip, pid, tid, address, weight, source = struct.unpack_from("QIIQQQ", data, pos + 8)
            if misc & 7 == 2 and source & 2 and address:
                samples.append(dict(ip=ip, pid=pid, tid=tid, address=address,
                                    weight=weight, source=source, level=(source >> 33) & 15))
        elif kind == 2:
            if size < 24:
                raise ValueError("truncated lost-sample record")
            lost += struct.unpack_from("Q", data, pos + 16)[0]
        pos += size
    return samples, lost


def read_maps(pid):
    maps = []
    for line in Path(f"/proc/{pid}/maps").read_text().splitlines():
        fields = line.split()
        start, end = (int(x, 16) for x in fields[0].split("-"))
        maps.append(dict(start=start, end=end, file_offset=int(fields[2], 16),
                         permissions=fields[1], path=fields[5] if len(fields) > 5 else "anonymous"))
    return maps


def locate(address, layout, maps, library):
    for area in layout:
        if area["start"] <= address < area["end"]:
            return area["name"], address - area["start"]
    for area in maps:
        if area["start"] <= address < area["end"]:
            name = area["path"]
            if name == str(library):
                name = "generated_readonly" if area["permissions"] == "r--p" else "generated_other"
            elif name == "[stack]":
                name = "stack"
            return name, address - area["start"] + area["file_offset"]
    return "unknown", address


def summarize(samples, layout, maps, library):
    counts = collections.defaultdict(collections.Counter)
    for sample in samples:
        name, offset = locate(sample["address"], layout, maps, library)
        sample.update(region=name, offset=offset)
        count = counts[name]
        count["loads"] += 1
        count[{1: "L1", 2: "L2", 3: "L3", 13: "RAM"}.get(sample["level"], "unknown_level")] += 1
        if sample["level"] in (2, 3, 13):
            count["beyond_L1"] += 1
            count["miss_weight"] += sample["weight"]
    return dict(sorted(counts.items(), key=lambda row: -row[1]["beyond_L1"]))


def build_harness(out, cc, cflags):
    """Instrument attachment in a temporary harness, leaving runtime sources intact."""
    source = (ROOT / "sims/native/mini-smoke.c").read_text()
    anchor = '#include "mini-loader.h"'
    helper = r'''
#include "schedule.h"
#include <unistd.h>
static int layout_attach(rds_sim *s,const char *path) {
    int status=rds_use_compiled(s,path);if(status)return status;
    FILE *f=fopen(getenv("RDS_LAYOUT_LOG"),"w");if(!f)return -1;
    fprintf(f,"arena %llu %zu\nff_bank_a %llu %zu\nff_bank_b %llu %zu\nobjects %llu %zu\n",
        (unsigned long long)(uintptr_t)s->arena,s->value_words*8,
        (unsigned long long)(uintptr_t)s->current,s->next_words*8,
        (unsigned long long)(uintptr_t)s->next,s->next_words*8,
        (unsigned long long)(uintptr_t)s->schedule->compiled_hot,s->schedule->compiled_hot_bytes);
    for(uint32_t i=0;i<s->nm;++i)fprintf(f,"sram_%u %llu %zu\n",i,
        (unsigned long long)(uintptr_t)s->mems[i].data,(size_t)s->mems[i].words*s->mems[i].depth*8);
    if(fclose(f))return -1;
    char ready=1;
    if(write(atoi(getenv("RDS_LAYOUT_READY_FD")),&ready,1)!=1 ||
       read(atoi(getenv("RDS_LAYOUT_GO_FD")),&ready,1)!=1)return -1;
    return 0;
}
'''
    if source.count(anchor) != 1 or source.count('rds_use_compiled(s,getenv("RDS_COMPILED"))') != 2:
        raise ValueError("MiniSoC attachment hooks changed; update the profiling adapter")
    source = source.replace('rds_use_compiled(s,getenv("RDS_COMPILED"))',
                            'layout_attach(s,getenv("RDS_COMPILED"))')
    source = source.replace(anchor, anchor + "\n" + helper)
    path = out / "layout-harness.c"
    path.write_text(source)
    runtime = sorted((ROOT / "rhodium/sim/runtime").glob("*.c"))
    command = [cc, "-std=c17", "-pthread", *cflags, "-I" + str(ROOT / "sims/native"),
               "-I" + str(ROOT / "rhodium/sim/runtime"), str(path), *map(str, runtime),
               "-ldl", "-o", str(out / "layout-harness")]
    subprocess.run(command, check=True)
    return command


def profile(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    command = build_harness(out, args.cc, shlex.split(args.cflags))
    library = args.library.resolve()
    ready_read, ready_write = os.pipe()
    go_read, go_write = os.pipe()
    env = {k: v for k, v in os.environ.items() if not k.startswith(("RDS_", "PMU_"))}
    env.update(RDS_WORKERS="1", RDS_FLAGS=str(args.flags), RDS_LOOP_ITERATIONS=str(args.loop),
               RDS_COMPILED=str(library), RDS_LAYOUT_LOG=str(out / "layout.txt"),
               RDS_LAYOUT_READY_FD=str(ready_write), RDS_LAYOUT_GO_FD=str(go_read))
    child = subprocess.Popen(["taskset", "-c", str(args.cpu), str(out / "layout-harness"),
                              str(args.model.resolve()), str(args.boots), "bench"], env=env,
                             pass_fds=(ready_write, go_read), stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    os.close(ready_write)
    os.close(go_read)
    fd, ring = -1, None
    try:
        if not select.select([ready_read], [], [], 60)[0] or os.read(ready_read, 1) != b"\1":
            raise RuntimeError("profiling harness did not reach attachment")
        maps = read_maps(child.pid)
        layout = []
        for line in (out / "layout.txt").read_text().splitlines():
            name, start, length = line.split()
            layout.append(dict(name=name, start=int(start), end=int(start) + int(length)))
        cache = Path(f"/sys/devices/system/cpu/cpu{args.cpu}/cache/index0")
        geometry = {key: (cache / key).read_text().strip() for key in
                    ("level", "type", "size", "coherency_line_size", "number_of_sets", "ways_of_associativity")}
        for area in layout:
            area["virtual_line_mod_sets"] = (area["start"] // int(geometry["coherency_line_size"])) % int(geometry["number_of_sets"])
        attr = ctypes.create_string_buffer(128)
        pmu_type = int(Path("/sys/bus/event_source/devices/ibs_op/type").read_text())
        # IBS has no hardware user/kernel filter. Filter sampled records below.
        struct.pack_into("IIQQQQQ", attr, 0, pmu_type, 128, 1 << 19,
                         args.period, SAMPLE_FIELDS, 0, 1)
        libc = ctypes.CDLL(None, use_errno=True)
        fd = libc.syscall(298, ctypes.byref(attr), child.pid, -1, -1, 0)
        if fd < 0:
            raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()))
        size = args.ring_mib * 1024 * 1024
        ring = mmap.mmap(fd, size + 4096, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ | mmap.PROT_WRITE)
        fcntl.ioctl(fd, 0x2400, 0)  # PERF_EVENT_IOC_ENABLE
        os.write(go_write, b"\1")
        stdout, stderr = child.communicate(timeout=max(60, args.boots / 10))
        fcntl.ioctl(fd, 0x2401, 0)  # PERF_EVENT_IOC_DISABLE
        if child.returncode:
            raise RuntimeError(f"profiling harness failed: {stderr}")
        timing = json.loads(stdout)
        if timing["workers"] != 1:
            raise ValueError("layout sampling currently requires one worker")
        head = struct.unpack_from("Q", ring, 1024)[0]
        offset = struct.unpack_from("Q", ring, 1040)[0] or 4096
        if head > size:
            raise RuntimeError("IBS ring overflow; increase --period or --ring-mib")
        samples, lost = decode_records(ring[offset:offset + head])
        if lost:
            raise RuntimeError(f"lost {lost} IBS samples; increase --period or --ring-mib")
        regions = summarize(samples, layout, maps, library)
        report = dict(note="Sampled loads, not exact miss counts. Attachment is excluded; warmup is included. "
                           "Bank labels identify physical allocations and remain fixed when pointers swap. "
                           "Only known L2/L3/RAM sources count as beyond L1.",
                      build_command=command, library=str(library),
                      library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                      model_sha256=hashlib.sha256(args.model.read_bytes()).hexdigest(),
                      configuration=dict(cpu=args.cpu, workers=1, flags=args.flags,
                                         boots=args.boots, loop=args.loop),
                      period=args.period, timing=timing, lost=lost,
                      cache_geometry=geometry, regions=regions, layout=layout, maps=maps, samples=samples)
        (out / "profile.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(dict(samples=len(samples), regions=regions), indent=2))
    finally:
        if child.poll() is None:
            child.kill()
            child.communicate()
        if ring is not None:
            ring.close()
        if fd >= 0:
            os.close(fd)
        os.close(ready_read)
        os.close(go_write)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cflags", default="-O3 -march=native -DNDEBUG")
    parser.add_argument("--flags", type=lambda x: int(x, 0), default=331792)
    parser.add_argument("--boots", type=int, default=1000)
    parser.add_argument("--loop", type=int, default=256)
    parser.add_argument("--cpu", type=int, default=0)
    parser.add_argument("--period", type=int, default=524288)
    parser.add_argument("--ring-mib", type=int, choices=(1, 2, 4, 8, 16), default=8)
    args = parser.parse_args()
    if os.uname().machine != "x86_64" or args.boots < 1 or args.period < 1:
        parser.error("requires x86-64 Linux and positive boots/period")
    profile(args)


if __name__ == "__main__":
    main()
