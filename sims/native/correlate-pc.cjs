// Maps sampled instruction pointers through DWARF to generated commands, DFG values, and flow boundaries.
'use strict';
const fs=require('fs'),cp=require('child_process'),path=require('path');
const [binary,sourceFile,planFile,modelFile,output,...samples]=process.argv.slice(2);
if(!output||!samples.length)throw Error('usage: node correlate-pc.cjs library source.c plan.json model.json output.json samples.bin ...');
const source=fs.readFileSync(sourceFile,'utf8'),plan=JSON.parse(fs.readFileSync(planFile)),model=JSON.parse(fs.readFileSync(modelFile));
if(plan.workers.length!==1)throw Error('requires one-worker plan');
const instructions=plan.workers[0].batches.flatMap(b=>b[2].map(i=>i[4]));
const definitions=new Map(model.operations.map(o=>[o[1],o])),boundary=new Map();
for(const [occurrence,kind,bindings]of model.contracts??[])for(const [binding,value]of bindings){
  if(value===4294967295)continue;
  if(!boundary.has(value))boundary.set(value,[]);
  boundary.get(value).push({path:model.occurrences[occurrence],kind,binding});
}
const commands=new Map();let covered=0;
for(const m of source.matchAll(/\/\* command (\d+) lane (\d+) instructions (\d+)\.\.(\d+) \*\/\nstatic [^\n]*?\b((?:bound|direct)_\d+)\(/g)){
  const values=instructions.slice(Number(m[3]),Number(m[4])),histogram={},objects=new Set();
  for(const v of values){const o=definitions.get(v);if(!o)throw Error('missing DFG definition '+v);const name=model.opcodes[o[0]];histogram[name]=(histogram[name]||0)+1;if(name==='object_query')objects.add(o[3][0]);}
  commands.set(m[5],{function:m[5],id:Number(m[1]),line:source.slice(0,m.index).split('\n').length+1,values,histogram,
    objects:[...objects].map(id=>({id,path:model.objects[id][5]})),contracts:values.flatMap(v=>boundary.get(v)||[]),samples:0});covered+=values.length;
}
if(covered!==instructions.length)throw Error('incomplete command/plan coverage');
const ips=new Map(),files=new Map();let total=0;
const realBinary=fs.realpathSync(binary);
for(const sample of samples){
  const maps=fs.readFileSync(sample+'.maps','utf8').trim().split('\n').map(line=>{
    const m=line.match(/^([\da-f]+)-([\da-f]+)\s+\S+\s+([\da-f]+)\s+\S+\s+\S+\s*(.*)$/);if(!m)throw Error('invalid mapping');
    return {lo:BigInt('0x'+m[1]),hi:BigInt('0x'+m[2]),offset:BigInt('0x'+m[3]),file:m[4]};
  });
  const data=fs.readFileSync(sample);if(data.length%8)throw Error('truncated sample stream');
  for(let i=0;i<data.length;i+=8){
    ++total;const pc=data.readBigUInt64LE(i),mapping=maps.find(m=>m.lo<=pc&&pc<m.hi),file=mapping?.file||'unknown';
    files.set(file,(files.get(file)||0)+1);if(file!==realBinary&&file!==path.resolve(binary))continue;
    const base=maps.find(m=>m.file===file&&m.offset===0n);if(!base)throw Error('missing ELF base mapping');
    const address='0x'+(pc-base.lo).toString(16);ips.set(address,(ips.get(address)||0)+1);
  }
}
if(!ips.size)throw Error('no samples in the requested library');
const addresses=[...ips.keys()];
const text=cp.execFileSync('llvm-symbolizer',['--obj='+binary,'--output-style=JSON'],{input:addresses.join('\n')+'\n',encoding:'utf8',maxBuffer:128*1024*1024});
const symbols=text.trim().split('\n').map(line=>JSON.parse(line));if(symbols.length!==addresses.length)throw Error('symbolizer coverage mismatch');
const categories={},locations=[];
for(let i=0;i<addresses.length;++i){
  const frames=symbols[i].Symbol,count=ips.get(addresses[i]);
  const frame=frames.find(f=>commands.has(f.FunctionName))||frames.at(-1),name=frame?.FunctionName||'unknown';
  const command=commands.get(name);if(command)command.samples+=count;
  const category=command?'combinational commands':name;categories[category]=(categories[category]||0)+count;
  locations.push({address:addresses[i],samples:count,frames});
}
const report={note:'Command-to-DFG mapping and contract boundary bindings are exact. Sample counts are statistical CPU attribution, not operation latency. Sampling includes setup/warmup; profiling time is not throughput.',
  binary,sourceFile,planFile,modelFile,total_samples:total,library_samples:[...ips.values()].reduce((a,b)=>a+b,0),files:[...files].sort((a,b)=>b[1]-a[1]),categories,
  commands:[...commands.values()].sort((a,b)=>b.samples-a.samples),locations:locations.sort((a,b)=>b.samples-a.samples)};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({total_samples:total,library_samples:report.library_samples,categories,top:report.commands.slice(0,12).map(c=>({function:c.function,samples:c.samples,operations:c.values.length,objects:c.objects}))},null,2));
