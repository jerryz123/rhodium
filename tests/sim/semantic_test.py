#!/usr/bin/env python3
# Checks typed aggregate lowering, independent feedback leaves, and invalid semantic inputs.
import copy
import json
import os
import random
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ['constant','copy','not','and','or','xor','add','sub','mul','shl','shru','shrs','eq','ult','slt','mux_lookup','onehot_mux','extract','zext','sext','pack','vector_index','vector_inject','vector_write_set','memory_read_async','decode','set_clear','balance','counter_step','object_query','alu','byte_merge']

def check_regions(compiler, directory, width=32, count=150):
    from runtime_test import Native, Options, library, check_aggregate
    lib=library(directory/'librhodium_sim.so');lib.compiled_cache={};lib.compiled=True;lib.release=True;lib.shape_limit=512
    m=dict(format='rhodium-simulation-ir-v1',opcodes=BASE,values=[1,width,width,6],operations=[],
           ports=[[0,0,'choose'],[0,1,'a'],[0,2,'b'],[0,3,'opcode']],registers=[],memories=[],writes=[],reads=[],assertions=[],objects=[],origins=[],inventory=[],occurrences=[],replicated_bytes=0,replicated_work=0)
    def op(code,w,args=(),imm=()):
        v=len(m['values']);m['values'].append(w)
        im=list(imm)
        if code==0:im += [0]*((w+63)//64-len(im))
        m['operations'].append([code,v,list(args),im]);return v
    x,y=1,2
    for i in range(count):
        k=op(0,width,imm=[2*i+3]);x=op(6,width,[op(8,width,[x,k]),2]);y=op(5,width,[op(6,width,[y,k]),1])
    selected=op(15,width,[0,y,x],[1]);m['ports'].append([1,selected,'result'])
    keys=[1,5,17,31,63];im=[len(keys),0]
    for k in keys:im += [k,63,1]
    member=op(25,1,[3],im);m['ports'].append([1,member,'member'])
    src=directory/f'semantic-regions-{width}.json';src.write_text(json.dumps(m));binary=src.with_suffix('.rsim')
    subprocess.run([compiler,'--input',str(src),'--output',str(binary),'--no-optimize'],check=True)
    rng=random.Random(8438)
    for workers in [1,4]:
        for flags in [16,16|262144,16|524288,16|262144|524288]:
            lib.options=Options(workers,flags);sim=Native(lib,binary)
            for _ in range(80):
                a,b=rng.getrandbits(width),rng.getrandbits(width);choose=rng.randrange(2);opcode=rng.randrange(64)
                sim.set('a',a);sim.set('b',b);sim.set('choose',choose);sim.set('opcode',opcode);sim.eval()
                x,y=a,b
                for i in range(count):x=(x*(2*i+3)+b)&((1<<width)-1);y=((y+2*i+3)^a)&((1<<width)-1)
                assert sim.get('result')==(x if choose else y)
                assert sim.get('member')==int(opcode in keys)
            sim.close()
    if (directory/'aggregate-semantic.rsim').exists():
        lib.release=False  # The aggregate oracle exercises debug partial-operation diagnostics.
        for workers in [1,4]:
            lib.options=Options(workers,262144|524288);check_aggregate(lib,directory,'aggregate-semantic')
    print('compiled 1/4-worker region, word-forwarding, and bitset decode checks passed')

def main():
    compiler = os.environ.get('RDS_OPTIMIZER', str(ROOT/'rhodium/sim/compiler/run.sh'))
    base = dict(format='rhodium-simulation-ir-v2',opcodes=BASE+['record_create','record_get','vector_create','vector_get','reinterpret'],
                types=[['bits',8],['record',[['hi',0],['lo',0]]]], values=[8,16,8,8],value_types=[0,1,0,0],
                operations=[[32,1,[0,2],[]],[33,2,[1],[0]],[33,3,[1],[1]]],
                ports=[[0,0,'x'],[1,3,'y']],registers=[],memories=[],writes=[],reads=[],assertions=[],objects=[],origins=[],inventory=[],occurrences=[],replicated_bytes=0,replicated_work=0,conditional_groups=[])
    # record.hi = x; record.lo = record.hi; whole-node graph cycles, leaves do not.
    with tempfile.TemporaryDirectory(prefix='rhodium-semantic-test-') as d:
        p=Path(d);src=p/'in.json';dst=p/'out.json'
        def run(m,good=True,extra=()):
            src.write_text(json.dumps(m));r=subprocess.run([compiler,'--input',str(src),'--report',str(dst),'--no-optimize',*extra],capture_output=True,text=True)
            assert (r.returncode==0)==good,r.stderr
            return json.loads(dst.read_text()) if good else None
        for enabled in [True,False]:
            m=run(base,extra=() if enabled else ("--no-semantic-cse",))
            assert m['ports'][0][1]==m['ports'][1][1],m
        # Demand crosses record packing and a vector field boundary.
        m=copy.deepcopy(base);m['types'] += [['vector',2,0]]
        m.update(values=[8,8,16,8],value_types=[0,0,2,0],operations=[[34,2,[0,1],[]],[35,3,[2],[1]]],ports=[[0,0,'a'],[0,1,'b'],[1,3,'y']])
        r=run(m);assert r['ports'][1][1]==r['ports'][2][1]
        for mutate in [lambda x:x['value_types'].__setitem__(1,0),
                       lambda x:x['operations'][1][3].__setitem__(0,2),
                       lambda x:x['operations'][0][2].__setitem__(0,2)]:
            bad=copy.deepcopy(base);mutate(bad);run(bad,False)
        print('semantic aggregate, feedback, vector projection and invalid-input checks passed')
    if len(sys.argv)>1:
        check_regions(compiler,Path(sys.argv[1]))
        check_regions(compiler,Path(sys.argv[1]),129,8)
if __name__=='__main__':main()
