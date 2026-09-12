#!/usr/bin/env python3
# Checks native FIFO payload elision, exact protocol traces, and compact object ownership.
import ctypes as C
import struct
import pathlib
import random
import sys
import os
sys.dont_write_bytecode = True
from runtime_test import Native, Options, library

root=pathlib.Path(sys.argv[1]); lib=library(root/'librhodium_sim.so'); lib.options=Options(1,0)
lib.compiled = bool(os.getenv('RDS_TEST_COMPILED'))
lib.compiled_cache, lib.shape_limit = {}, 512
for width in (8,65,4096):
 for depth in (1,3,1024):
  for pipe in (0,1):
   for flow in (0,1):
    name=f'fifo-{width}-{depth}-{pipe}{flow}'
    full=Native(lib,root/f'{name}.rsim'); tokens=Native(lib,root/f'{name}-tokens.rsim')
    fs=lib.rds_get_stats(full.ptr); ts=lib.rds_get_stats(tokens.ptr)
    assert fs.objects==ts.objects==1 and fs.registers==fs.memories==0
    assert fs.payload_bytes==((width+63)//64)*8*(depth+1)
    assert ts.payload_bytes==0 and ts.object_bytes<512
    rng=random.Random(187); memory=[0]*depth; count=rp=wp=0
    for cycle in range(400):
     reset=int(cycle in (0,149)); valid=int(cycle<200 or rng.randrange(2)); ready=int(cycle>80 and rng.randrange(2)); data=rng.getrandbits(width)
     expected_ready=int(count<depth or pipe and ready)
     expected_valid=int(count>0 or flow and valid)
     for sim in (full,tokens):
      sim.set('reset',reset); sim.set('ingress_in',valid<<width|data); sim.set('egress_in',ready); sim.eval()
      assert sim.get('count')==count and sim.get('ingress_out')==expected_ready
      output=sim.get('egress_out'); assert output>>width==expected_valid
      expected_data=0 if sim is tokens else data if flow and not count else memory[rp]
      assert output&((1<<width)-1)==expected_data
     enq=valid and expected_ready and not(flow and not count and ready); deq=count>0 and ready
     if enq: memory[wp]=data; wp=(wp+1)%depth
     if deq: rp=(rp+1)%depth
     count+=int(enq)-int(deq)
     if reset: count=rp=wp=0
     full.advance(); tokens.advance()
    full.close(); tokens.close()
print('36 FIFO configurations: functional payloads and payload-free control traces passed')
for workers in (1,4):
 lib.options=Options(workers,0)
 sim=Native(lib,root/'offer.rsim'); occupied=payload=0
 for cycle in range(400):
  reset=int(cycle in (0,51)); update=cycle%3!=0; ready=cycle%5!=0; incoming=cycle<<60
  sim.set('reset',reset);sim.set('update_in',int(update)<<65|(incoming&((1<<65)-1)));sim.set('egress_in',int(ready));sim.eval()
  assert sim.get('egress_out')==occupied<<65|payload
  if update: payload=incoming&((1<<65)-1)
  occupied=0 if reset else 1 if update else 0 if ready else occupied
  sim.advance()
 sim.close()
 sim=Native(lib,root/'rr.rsim'); cursor=0; rng=random.Random(5)
 for cycle in range(400):
  reset=int(cycle in (0,55)); mask=rng.randrange(8); ready=rng.randrange(2)
  sim.set('reset',reset);sim.set('egress_in',ready)
  for i in range(3):sim.set(f'ingress_{i}_in',((mask>>i)&1)<<8|i+10)
  sim.eval(); chosen=next(((cursor+i)%3 for i in range(3) if mask>>((cursor+i)%3)&1),None)
  assert sim.get('chosen')==(chosen or 0)
  assert sim.get('egress_out')==int(chosen is not None)<<8|(10+(chosen or 0))
  for i in range(3):assert sim.get(f'ingress_{i}_out')==int(chosen==i and ready)
  cursor=0 if reset else (chosen+1)%3 if chosen is not None and ready else cursor
  sim.advance()
 sim.close()
print('Offer replacement and transfer-based round-robin rotation passed')

for workers in (1,4):
 for name in ('valid-pipe','broadcast','ctrl-broadcast','packet-rr'):
  lib.options=Options(workers,0)
  full=Native(lib,root/f'{name}.rsim'); tokens=Native(lib,root/f'{name}-tokens.rsim')
  lib.options=Options(1,1); raw=Native(lib,root/f'{name}.rsim')
  assert lib.rds_get_stats(tokens.ptr).payload_bytes==0
  rng=random.Random(1851)
  for cycle in range(700):
   stimulus={'reset':int(cycle in (0,155,336))}
   if name=='packet-rr':
    stimulus.update({f'ingress_{i}_in':rng.randrange(512) for i in range(3)})
    stimulus.update(egress_in=rng.randrange(2),ends_packet=rng.randrange(8))
    outputs={'chosen':None,'egress_out':8,**{f'ingress_{i}_out':None for i in range(3)}}
   else:
    width=0 if name=='ctrl-broadcast' else 65
    stimulus['ingress_in']=rng.getrandbits(width+1)
    outputs={'egress_out':width} if name=='valid-pipe' else {'ingress_out':None,**{f'egress_{i}_out':width for i in range(3)}}
    if name!='valid-pipe':stimulus.update({f'egress_{i}_in':rng.randrange(2) for i in range(3)})
   for sim in (full,raw,tokens):
    for key,value in stimulus.items():sim.set(key,value)
    sim.eval()
   for key,width in outputs.items():
    expected=raw.get(key)
    assert full.get(key)==expected,(name,cycle,key,full.get(key),expected)
    token_expected=expected if width is None else expected>>width<<width
    assert tokens.get(key)==token_expected,(name,cycle,key)
   for sim in (full,raw,tokens):sim.advance()
  for sim in (full,raw,tokens):sim.close()
print('ValidPipe, Broadcast, CtrlBroadcast, and packet RR match raw RTL across resets, bubbles and stalls')

# A functional FIFO descriptor must provide a correctly typed payload binding.
image=bytearray((root/'fifo-8-3-00.rsim').read_bytes())
version,*counts=struct.unpack_from('<12I',image,8)
assert version==2
nv,no,na,ni,nr,nm,nw,ns,nc,np,nx=counts
position=56+4*nv+24*no+4*na+8*ni+16*nr+8*nm+24*nw+20*ns
for _ in range(nc):
 position+=12
 length=struct.unpack_from('<I',image,position)[0]; position+=4+length
for _ in range(np):
 position+=8
 length=struct.unpack_from('<I',image,position)[0]; position+=4+length
assert nx==1
lib.options=Options(4,0)
for offset,value in ((position+28,0xffffffff),(position+8,0),(position+12,128)):
 broken=bytearray(image);struct.pack_into('<I',broken,offset,value)
 path=root/'invalid-native-object.rsim';path.write_bytes(broken)
 error=C.create_string_buffer(512)
 loaded=lib.rds_load_with_options(str(path).encode(),C.byref(lib.options),error,len(error))
 assert not loaded,(offset,error.value)
print('Native descriptor validation rejects missing functional payloads, zero capacity and invalid flags')

lib.options=Options(4,32)
base=Native(lib,root/'shared-base.rsim'); replicated=Native(lib,root/'shared-replicated.rsim'); limited=Native(lib,root/'shared-limited.rsim')
assert lib.rds_get_stats(base.ptr).components==1
assert lib.rds_get_stats(replicated.ptr).workers==4
assert lib.rds_get_stats(replicated.ptr).components>=8
lib.options=Options(4,64)
automatic=Native(lib,root/'shared-base.rsim')
assert 1 < lib.rds_get_stats(automatic.ptr).workers <= 4
state=[0]*8; rng=random.Random(190)
for cycle in range(800):
 reset=int(cycle in (0,217));amount=rng.getrandbits(32);mask=rng.getrandbits(32)
 for sim in (base,replicated,limited,automatic):
  for key,value in dict(reset=reset,amount=amount,mask=mask).items():sim.set(key,value)
  sim.eval()
  assert [sim.get(f'state{i}') for i in range(8)]==state
  sim.advance()
 state=list(range(8)) if reset else [(v+(amount^mask))&0xffffffff for v in state]
for sim in (base,replicated,limited,automatic):sim.close()
base=Native(lib,root/'shared-native-base.rsim'); replicated=Native(lib,root/'shared-native-replicated.rsim')
assert lib.rds_get_stats(replicated.ptr).objects==1
count=0; state=[0]*8
for cycle in range(800):
 reset=int(cycle in (0,287));push=rng.randrange(2);pop=rng.randrange(2)
 for sim in (base,replicated):
  for key,value in dict(reset=reset,push=push,pop=pop).items():sim.set(key,value)
  sim.eval()
  assert sim.get('totals_out')==sum(v<<(32*i) for i,v in enumerate(state))
  sim.advance()
 state=[0]*8 if reset else [(v+count)&0xffffffff for v in state]
 count=0 if reset else count+int(push and count<3)-int(pop and count>0)
for sim in (base,replicated):sim.close()
print('Bounded snapshot replication splits shared cones across four workers and preserves one FIFO update owner')
