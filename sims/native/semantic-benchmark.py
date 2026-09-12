#!/usr/bin/env python3
# Builds and measures semantic-IR and static-region ablations against matching one/four-thread Verilator O2.
import argparse
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('directory',type=Path)
    ap.add_argument('--baseline-ir',type=Path,required=True)
    ap.add_argument('--sv',type=Path,required=True)
    ap.add_argument('--trials',type=int,default=5)
    ap.add_argument('--native-cflags',default='-O3 -march=native')
    ap.add_argument('--reuse-build',action='store_true')
    args=ap.parse_args();d=args.directory.resolve();d.mkdir(parents=True,exist_ok=True)
    cpus=sorted(os.sched_getaffinity(0))[:4]
    if len(cpus)!=4:ap.error('four permitted CPUs required')
    env={k:v for k,v in os.environ.items() if not k.startswith('RDS_')}
    opt=subprocess.check_output([str(ROOT/'rhodium/sim/compiler/run.sh'),'--print-path'],text=True).strip()
    def run(cmd,log=None,e=None):
        if log:
            with (d/log).open('w') as f:subprocess.run(list(map(str,cmd)),env=e or env,check=True,stdout=f,stderr=subprocess.STDOUT)
        else:subprocess.run(list(map(str,cmd)),env=e or env,check=True)
    models={'legacy':args.baseline_ir.resolve(),'semantic':d/'mini.extracted.json'}
    variants={}
    for workers in [1,4]:
        base=69648 if workers==1 else 86608
        for name,extra in [('base',0),('wide',262144),('select',524288),('both',786432)]:
            variants[f'semantic_{workers}_{name}']=(workers,base|extra,'semantic')
        variants[f'legacy_{workers}']=(workers,base,'legacy')
    cc=os.getenv('CC','cc');cflags=shlex.split(args.native_cflags)+['-DNDEBUG']
    if not args.reuse_build:
        for name,source in models.items():
            run([opt,'--input',source,'--output',d/f'{name}.rsim','--report',d/f'{name}.optimized.json','--timing',d/f'{name}.timing.json','--dump-passes',d/f'{name}-passes','--release'])
        runtime=sorted((ROOT/'rhodium/sim/runtime').glob('*.c'))
        for source,target in [('compile-model.c','compile-model'),('mini-smoke.c','mini-smoke')]:
            run([cc,'-std=c17','-pthread',*cflags,'-Wall','-Wextra','-Werror',ROOT/'sims/native'/source,*runtime,'-ldl','-o',d/target],target+'-build.log')
        for name,(workers,flags,model) in variants.items():
            e=env|dict(RDS_WORKERS=str(workers),RDS_FLAGS=str(flags),RDS_PLAN_REPORT=str(d/f'{name}.plan.json'))
            run([d/'compile-model',d/f'{model}.rsim',d/f'{name}.c'],name+'-emit.log',e)
            run([cc,'-std=c17',*cflags,'-fPIC','-shared',d/f'{name}.c','-o',d/f'{name}.so'],name+'-build.log')
        for workers in [1,4]:
            run(['verilator','--cc','-O2','--threads',workers,'--no-assert','--Wno-UNOPTFLAT','--Wno-SYMRSVDWORD','--top-module','SoCHarness','--Mdir',d/f'verilated-{workers}','--exe',ROOT/'sims/native/verilator-smoke.cpp','-CFLAGS',f'-O2 -DNDEBUG -DRDS_VERILATOR_THREADS={workers}','-MAKEFLAGS','OPT_FAST=-O2 OPT_SLOW=-O2 OPT_GLOBAL=-O2','--build','-j','2',args.sv.resolve()],f'verilator-{workers}-build.log')
    modes={}
    for name,(workers,flags,model) in variants.items():
        e=env|dict(RDS_WORKERS=str(workers),RDS_FLAGS=str(flags),RDS_COMPILED=str(d/f'{name}.so'))
        cmd=['taskset','-c',','.join(map(str,cpus[:workers])),str(d/'mini-smoke'),str(d/f'{model}.rsim')]
        stats=json.loads(subprocess.check_output(cmd+['0','stats'],env=e,text=True))
        if stats['workers']!=workers:raise RuntimeError(f'{name}: requested {workers}, actual {stats["workers"]}')
        modes[name]=(cmd,e,workers);(d/f'{name}.stats.json').write_text(json.dumps(stats,indent=2))
    for workers in [1,4]:
        modes[f'verilator_{workers}']=(['taskset','-c',','.join(map(str,cpus[:workers])),str(d/f'verilated-{workers}/VSoCHarness')],env,workers)
    results=[];expected={}
    # Check complete boots before timing; the same harness validates status and digest during timing.
    for loop,boots in [(0,250),(256,12)]:
        for trial in range(args.trials):
            order=list(modes);order=order[trial%len(order):]+order[:trial%len(order)]
            if trial%2:order.reverse()
            for name in order:
                cmd,e,workers=modes[name]
                row=json.loads(subprocess.check_output(cmd+[str(boots),'bench'],env=e|{'RDS_LOOP_ITERATIONS':str(loop)},text=True))
                signature=(row['cycles'],row['polls'],row['digest'])
                if loop in expected and signature!=expected[loop]:raise RuntimeError(f'boot mismatch {name}: {signature} != {expected[loop]}')
                expected[loop]=signature
                if name.startswith('verilator'):
                    if row.get('workers')!=workers or row.get('context_threads')!=workers:raise RuntimeError(f'thread mismatch {row}')
                row.update(mode=name,loop=loop,trial=trial,boots=boots,rate=row['cycles']/row['seconds']);results.append(row)
        print('completed workload',loop,flush=True)
    report=dict(cpus=cpus,trials=args.trials,native_cflags=cflags,verilator_cflags='-O2 -DNDEBUG; all OPT_*=-O2',sv=str(args.sv.resolve()),warmup_boots=5,variants=variants,rows=results)
    report['medians']={str(loop):{name:statistics.median(r['rate'] for r in results if r['loop']==loop and r['mode']==name) for name in modes} for loop in [0,256]}
    (d/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['medians'],indent=2))
if __name__=='__main__':main()
