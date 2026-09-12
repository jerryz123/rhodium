#!/usr/bin/env python3
# Checks standard-library snapshots, handshakes, and updates against independent models.
import os
import pathlib
import random
import sys
sys.dont_write_bytecode = True
from runtime_test import Native, Options, library


def scoreboard(lib, directory):
    sim = Native(lib, directory / "std-scoreboard.rsim")
    rng, busy = random.Random(711), 0
    for cycle in range(600):
        reset = int(cycle in (0, 117))
        events = [(sv, si, cv, ci) for sv in range(2) for si in range(3)
                  for cv in range(2) for ci in range(3)
                  if (not sv or not (busy >> si & 1) or (cv and si == ci))
                  and (not cv or (busy >> ci & 1) or (sv and si == ci))]
        sv, si, cv, ci = rng.choice(events)
        sim.set("reset", reset)
        sim.set("set_in", sv << 2 | si)
        sim.set("clear_in", cv << 2 | ci)
        sim.eval()
        assert sim.get("busy") == busy
        sim.advance()
        busy = 0 if reset else (busy | ((1 << si) if sv else 0)) & ~((1 << ci) if cv else 0)
    sim.set("reset", 0)
    sim.set("set_in", 7)
    sim.set("clear_in", 0)
    sim.eval()
    assert lib.rds_advance(sim.ptr) != 0
    assert b"scoreboard_set_index_in_range" in lib.rds_error(sim.ptr)
    sim.eval()
    assert sim.get("busy") == busy
    sim.close()


def queue(lib, directory, name, depth, pipe=False, flow=False):
    sim = Native(lib, directory / f"{name}.rsim")
    memory, count, rp, wp = [0] * depth, 0, 0, 0
    rng = random.Random(803)
    for cycle in range(800):
        reset = int(cycle in (0, 149, 501))
        valid, ready, data = rng.randrange(2), rng.randrange(2), rng.randrange(256)
        input_ready = int(count < depth or (pipe and ready))
        output_valid = int(count != 0 or (flow and valid))
        output_data = data if flow and count == 0 else memory[rp]
        for key, val in dict(reset=reset, ingress_in=valid << 8 | data, egress_in=ready).items():
            sim.set(key, val)
        sim.eval()
        assert sim.get("count") == count
        assert sim.get("ingress_out") == input_ready
        assert sim.get("egress_out") == output_valid << 8 | output_data
        enqueue = valid and input_ready and not (flow and count == 0 and ready)
        dequeue = count != 0 and ready
        if enqueue:
            memory[wp] = data
            wp = (wp + 1) % depth
        if dequeue:
            rp = (rp + 1) % depth
        count += int(enqueue) - int(dequeue)
        if reset:
            count = rp = wp = 0
        sim.advance()
    sim.close()


def pipe(lib, directory):
    sim = Native(lib, directory / "std-pipe.rsim")
    valid, data = [0, 0], [0, 0]
    rng = random.Random(148)
    for cycle in range(600):
        reset, iv, ir, payload = int(cycle in (0, 203)), rng.randrange(2), rng.randrange(2), rng.randrange(256)
        sim.set("reset", reset)
        sim.set("ingress_in", iv << 8 | payload)
        sim.set("egress_in", ir)
        sim.eval()
        ready = [0, int(not valid[1] or ir)]
        ready[0] = int(not valid[0] or ready[1])
        assert sim.get("ingress_out") == ready[0]
        assert sim.get("egress_out") == valid[1] << 8 | data[1]
        src_valid, src_data = [iv, valid[0]], [payload, data[0]]
        for i in range(2):
            if ready[i]:
                valid[i] = src_valid[i]
                if src_valid[i]:
                    data[i] = src_data[i]
        if reset:
            valid = [0, 0]
        sim.advance()
    sim.close()


def arbiter(lib, directory):
    sim = Native(lib, directory / "std-arbiter.rsim")
    rng = random.Random(389)
    for _ in range(600):
        mask, ready = rng.randrange(8), rng.randrange(2)
        data = [rng.randrange(256) for _ in range(3)]
        for i in range(3):
            sim.set(f"ingress_{i}_in", (mask >> i & 1) << 8 | data[i])
        sim.set("egress_in", ready)
        sim.eval()
        chosen = (mask & -mask).bit_length() - 1 if mask else 0
        assert sim.get("chosen") == chosen
        assert sim.get("egress_out") == int(bool(mask)) << 8 | data[chosen]
        for i in range(3):
            assert sim.get(f"ingress_{i}_out") == int(bool(mask) and ready and i == chosen)
    sim.close()


def credit(lib, directory):
    sim = Native(lib, directory / "std-credit.rsim")
    rng = random.Random(567)
    memory, rp, wp, count, credits, reserved = [0, 0], 0, 0, 0, 0, 0
    for cycle in range(800):
        reset = int(cycle in (0, 351))
        valid, ready, allow, data = rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(256)
        if reset:
            valid = ready = allow = 0
        for key, val in dict(reset=reset, grant_enable=allow, ingress_in=valid << 8 | data, egress_in=ready).items():
            sim.set(key, val)
        sim.eval()
        assert sim.get("credit_count") == credits
        assert sim.get("reserved") == reserved
        assert sim.get("count") == count
        assert sim.get("ingress_out") == int(credits > 0)
        assert sim.get("egress_out") == int(count > 0) << 8 | memory[rp]
        send, dequeue = valid and credits > 0, ready and count > 0
        grant = allow and (reserved < 2 or dequeue)
        if send:
            memory[wp] = data
            wp = (wp + 1) % 2
        if dequeue:
            rp = (rp + 1) % 2
        credits += int(grant) - int(send)
        reserved += int(grant) - int(dequeue)
        count += int(send) - int(dequeue)
        if reset:
            credits = reserved = count = rp = wp = 0
        sim.advance()
    sim.close()


directory = pathlib.Path(sys.argv[1])
lib = library(directory / "librhodium_sim.so")
modes = [("raw reference", 1, 1), ("semantic C", 1, 0), ("semantic parallel", 4, 0)]
modes += [("compiled C", 1, 0), ("compiled epochs", 4, 64)]
lib.compiled_cache, lib.shape_limit = {}, 512
if os.getenv("RDS_TEST_ASM"):
    modes.append(("semantic assembly", 1, 8))
for label, workers, flags in modes:
    lib.compiled = "compiled" in label
    lib.options = Options(workers, flags)
    scoreboard(lib, directory)
    queue(lib, directory, "std-queue", 3)
    queue(lib, directory, "std-queue-one", 1)
    queue(lib, directory, "std-queue-options", 2, pipe=True, flow=True)
    pipe(lib, directory)
    arbiter(lib, directory)
    credit(lib, directory)
    print(f"{label}: Queue, Pipe, Scoreboard, Arbiter, and credited-flow observations passed")
