<!-- Documents the no-shell Buildroot Linux smoke workload and its build/run boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Buildroot Linux smoke

This BR2_EXTERNAL port builds a small Linux system whose only userspace workload
is a static `/init`: open `/dev/console`, report reaching PID 1, and power off.
There is no BusyBox, shell, login, network, or disk filesystem. Read
[`../DEVELOPING.md`](../DEVELOPING.md#buildroot-linux-smoke) before changing the port.

## Build and run

Buildroot image construction requires a Linux host with the prerequisites in
the [upstream manual](https://buildroot.org/downloads/manual/manual.html#requirement).
Use the repository's usual simulator setup, then:

```sh
moon run rhodium:linux-setup
SOC=simple-spike-rva23 moon run rhodium:linux-elfs
```

`linux-elfs` builds without running; `sim-linux-run` runs an existing manifest
without building software. Select `simple-rv5stage-rva23` or
`simple-rv2wide-rva23` for RTL, using the normal simulator options. The intended
initial qualification order is Spike, RV5Stage, then RV2Wide.

Buildroot is pinned at tag `2026.08.1` (gitlink `7fdeadb3b9f12f78e5fccca914d7fef47090ab9a`);
Linux is checksum-pinned to `6.18.7`. The existing OpenSBI pin and FW_JUMP builder
are reused. Buildroot supplies the static musl userspace toolchain and projects
the selected target's compiler ISA, including vector extensions, into its config.

## Boot and artifact contract

The target must have one bootable RV64 hart, LP64D, vector/FP and bit-manipulation
capabilities, Sv39, M/S/U modes, and at least 128 MiB of contiguous boot RAM.
Selection is capability-based, not tied to a core name. The smoke limits Linux
to 128 MiB with its built-in kernel command line, without changing the hardware
RAM map or DTB. Its initramfs is embedded in
the kernel; there is no separate initrd load address.

FESVR loads `fw_jump.elf` plus a physical-load `linux.elf` wrapper. OpenSBI copies
the SoC's BootROM DTB from `a1` to `FW_JUMP_FDT_ADDR`, applies its normal
fixups, and enters Linux in S-mode. No Linux-specific DTB is generated.
Linux uses the SBI console and reset services; only OpenSBI owns HTIF mailboxes.
Passing requires the userspace marker, kernel poweroff message, successful
simulator termination, and no panic/oops/failure marker.

Outputs live under `PROGRAM_BUILD_ROOT/<soc>/linux` (override `LINUX_OUTPUT`).
`LINUX_DOWNLOADS` shares upstream downloads; `LINUX_JOBS` defaults to four.
The normal runner writes logs and JSON/JUnit results. `LINUX_TIMEOUT=1800` and
`LINUX_MAX_CYCLES=500000000` are provisional safety limits, not measured runtime.

macOS can generate descriptors and simulate a Linux-built bundle. On
the Mac, generate the execution inputs:

```sh
SOC=simple-spike-rva23 moon run rhodium:local-target
```

Transfer the generated descriptor to the Linux build host and use
`PREBUILT_PROGRAM_TARGET=/absolute/path/target.json` with `linux-elfs`; this avoids
requiring Rhodium elaboration on the image-building host. Package the manifest
and every referenced file with the common archive tool:

```sh
python3 sims/program-test/archive.py --manifest /absolute/path/linux/manifest.json \
  --archive /absolute/path/linux-smoke.tar.gz
```

Transfer and extract that archive into an empty directory on the simulation host.
Bind and run with the existing artifact flow:

```sh
python3 sw/build/bind.py --manifest /absolute/path/bundle/manifest.json \
  --target /absolute/path/execution/program-target.json \
  --output /absolute/path/bundle/run-manifest.json
SOC=simple-spike-rva23 PROGRAM_MANIFEST=/absolute/path/bundle/run-manifest.json moon run rhodium:sim-linux-run
```

Each simulator provides its own canonical hardware DTB at boot.
Build directories are disposable; distribute referenced artifacts, not the
toolchain cache. Included license notices do not replace applicable source
distribution obligations; see [`../../THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).

## Qualification limits

CI builds images on x86 Linux for compatible Simple RVA23 targets. Download
`platform-elfs-linux-<build-spec-fingerprint>-<commit>` from the CI run; its
`suite.tar.gz` uses the archive and binding contract above. Configurations with
identical build requirements share an image. Build diagnostics are published as
`linux-build-linux-<build-spec-fingerprint>-<commit>`.

CI checks compilation, configuration, ELF layout, checksums, archive extraction,
and target compatibility only. It does not boot Linux or test shutdown. A green
image-build job is not a qualified Linux platform. Boot qualification remains
deferred; multihart, networking, persistent storage, and interactive userspace
are outside this initial smoke profile.
