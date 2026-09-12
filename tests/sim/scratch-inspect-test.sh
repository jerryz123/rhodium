#!/usr/bin/env bash
# Checks spill eligibility, SIMD alias accounting, and sampled-load attribution in the C++ inspector.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
probe_temp=$(mktemp -d)
trap 'rm -rf "$probe_temp"' EXIT
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror "$repo_root/sims/native/scratch-inspect.cpp" -o "$probe_temp/inspect"
cat > "$probe_temp/input.s" <<'ASM'
# Describes leaf, calling, and overlapping spill slots for the parser.
.type bound_0,@function
bound_0:
 movq %rax, (%rsp) # 8-byte Spill
 movq (%rsp), %rbx # 8-byte Reload
 movq (%rsp), %rcx # 8-byte Reload
 vmovq %rax, %xmm0
 vpxor %ymm0, %ymm0, %ymm0
 vpxorq %zmm0, %zmm0, %zmm0
.type bound_1,@function
bound_1:
 movq %rax, 16(%rsp) # 8-byte Spill
 movq 16(%rsp), %rbx # 8-byte Reload
 movq 16(%rsp), %rcx # 8-byte Reload
	callq external
.type bound_2,@function
bound_2:
 vmovdqu %xmm2, (%rsp) # 16-byte Spill
 movq %rax, 8(%rsp) # 8-byte Spill
 movq 8(%rsp), %rbx # 8-byte Reload
 movq 8(%rsp), %rcx # 8-byte Reload
ASM
cat > "$probe_temp/disassembly.txt" <<'ASM'
# Associates machine instructions with the compiler spill offsets.
00004000 <bound_0>:
 4010: mov rbx,QWORD PTR [rsp]
 4020: ret
00004030 <bound_1>:
 4040: mov rbx,QWORD PTR [rsp+0x10]
ASM
cat > "$probe_temp/profile.json" <<'JSON'
{"library":"/model.so","library_sha256":"fixture","maps":[{"path":"/model.so","start":4096,"end":8192,"file_offset":0}],"samples":[{"ip":4112,"region":"stack"},{"ip":4112,"region":"stack"},{"ip":4160,"region":"stack"},{"ip":4112,"region":"arena"}]}
JSON
node - "$probe_temp" <<'JS'
// Supplies a valid minimal ELF mapping whose virtual addresses differ from file offsets.
const fs=require('fs'),dir=process.argv[2],elf=Buffer.alloc(4096);
elf.set([0x7f,69,76,70,2,1,1]);elf.writeUInt16LE(3,16);elf.writeUInt16LE(62,18);elf.writeUInt32LE(1,20);
elf.writeBigUInt64LE(64n,32);elf.writeUInt16LE(64,52);elf.writeUInt16LE(56,54);elf.writeUInt16LE(1,56);
elf.writeUInt32LE(1,64);elf.writeUInt32LE(5,68);elf.writeBigUInt64LE(0x4000n,80);
elf.writeBigUInt64LE(4096n,96);elf.writeBigUInt64LE(4096n,104);elf.writeBigUInt64LE(4096n,112);
fs.writeFileSync(dir+'/model.so',elf);
const profile=JSON.parse(fs.readFileSync(dir+'/profile.json'));profile.library=dir+'/model.so';profile.maps[0].path=profile.library;
profile.samples.push({ip:4128,region:'stack'});fs.writeFileSync(dir+'/profile.json',JSON.stringify(profile));
JS
"$probe_temp/inspect" "$probe_temp/input.s" "$probe_temp/profile.json" "$probe_temp/disassembly.txt" "$probe_temp/output.json"
node - "$probe_temp/output.json" <<'JS'
// Validates supported inspection results and conservative overlap rejection.
const assert=require('assert/strict'),fs=require('fs'),r=JSON.parse(fs.readFileSync(process.argv[2]));
assert.equal(r.all_load_samples,5);assert.equal(r.stack_load_samples,4);assert.equal(r.eligible_load_samples,2);
assert.equal(r.stack_opcode_samples.ret,1);
assert.equal(r.eligible_static_stores,1);assert.equal(r.eligible_static_reloads,2);
const functions=Object.fromEntries(r.functions.map(f=>[f.function,f]));
assert.deepEqual(functions.bound_0.used_vector_indices,[0]);assert.equal(functions.bound_0.unused_vector_indices.length,31);
assert.equal(functions.bound_1.calls,true);assert.ok(functions.bound_1.slots.every(s=>!s.eligible));
assert.ok(functions.bound_2.slots.every(s=>!s.eligible));
console.log('scratch inspector checks passed');
JS
if "$probe_temp/inspect" "$probe_temp/missing.s" "$probe_temp/profile.json" "$probe_temp/disassembly.txt" "$probe_temp/bad.json" >/dev/null 2>&1; then
  echo 'missing input was accepted' >&2
  exit 1
fi
