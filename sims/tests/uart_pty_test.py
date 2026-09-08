#!/usr/bin/env python3
"""Checks real SoC UART pins against an external raw PTY client, with bounded execution."""

import errno
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time


def run(simulator, payload):
    transcript = bytearray()
    uart_fd = None
    expected = b"R"
    ready = False
    exchanged = 0
    acknowledged = False
    deadline = time.monotonic() + 120
    process = subprocess.Popen(
        [str(Path(simulator).resolve()), "+max-cycles=50000000", str(Path(payload).resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ, "log")
            while True:
                if time.monotonic() >= deadline:
                    raise RuntimeError("UART PTY exchange exceeded its wall-clock deadline")
                for key, _ in selector.select(0.1):
                    if key.data == "log":
                        data = os.read(process.stdout.fileno(), 4096)
                        if not data:
                            selector.unregister(process.stdout)
                            continue
                        transcript.extend(data)
                        if uart_fd is None:
                            match = re.search(rb"UART DPI model 0 PTY: (\S+)\r?\n", transcript)
                            if match:
                                uart_fd = os.open(os.fsdecode(match[1]), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                                selector.register(uart_fd, selectors.EVENT_READ, "uart")
                    else:
                        try:
                            data = os.read(uart_fd, 4096)
                        except OSError as error:
                            if error.errno in (errno.EAGAIN, errno.EIO):
                                continue
                            raise
                        if not data:
                            continue
                        if data != expected:
                            raise RuntimeError(f"UART reply {data!r}, expected {expected!r} at byte {exchanged}")
                        if acknowledged:
                            raise RuntimeError("Unexpected UART output after the final reply")
                        if not ready:
                            ready = True
                            outgoing = 0
                        else:
                            exchanged += 1
                            outgoing = exchanged
                        if exchanged == 256:
                            outgoing = 0x5a
                            acknowledged = True
                        if os.write(uart_fd, bytes([outgoing])) != 1:
                            raise RuntimeError("PTY input write was incomplete")
                        expected = bytes([outgoing ^ 0xa5])
                if process.poll() is not None:
                    transcript.extend(process.stdout.read())
                    if process.returncode != 0 or not acknowledged:
                        raise RuntimeError(f"Simulator exited {process.returncode} after {exchanged}/256 bytes")
                    if b"framing error" in transcript:
                        raise RuntimeError("UART DPI reported a framing error")
                    print("SoC PTY UART passed: all 256 byte values in both directions")
                    return
    except Exception:
        sys.stderr.write(transcript.decode(errors="replace"))
        raise
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
        if uart_fd is not None:
            os.close(uart_fd)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: uart_pty_test.py SIMULATOR PAYLOAD")
    run(*sys.argv[1:])
