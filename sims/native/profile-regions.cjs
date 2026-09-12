// Instruments generated command execution and correlates skipped work with the retained operation graph.
'use strict';
const fs = require('fs');
const [mode, ...args] = process.argv.slice(2);
function fail(message) { throw new Error(message); }
if (mode === 'emit' && args.length === 4) {
  const [source, planFile, modelFile, prefix] = args;
  let code = fs.readFileSync(source, 'utf8');
  const plan = JSON.parse(fs.readFileSync(planFile)), model = JSON.parse(fs.readFileSync(modelFile));
  if (plan.workers.length !== 1) fail('region activity currently requires one worker');
  const instructions = plan.workers.flatMap(w => w.batches.flatMap(b => b[2].map(i => ({code:b[0], width:b[1], origin:i[4]}))));
  const definitions = new Map(model.operations.map(o => [o[1],o]));
  const boundary = new Map();
  for(const [occurrence,kind,bindings] of model.contracts || [])for(const [name,value] of bindings){
    if(value===4294967295)continue;
    if(!boundary.has(value))boundary.set(value,[]);
    boundary.get(value).push({path:model.occurrences[occurrence],kind,binding:name});
  }
  const commands = [];
  for (const match of code.matchAll(/\/\* command (\d+) lane (\d+) instructions (\d+)\.\.(\d+) \*\/\nstatic [^\n]*?\b((?:bound|direct)_\d+)\(/g)) {
    const [id,lane,begin,end] = match.slice(1,5).map(Number);
    const ops = instructions.slice(begin,end), histogram = {};
    for (const op of ops) {
      const def = definitions.get(op.origin);
      if (!def) fail('missing DFG definition for command '+id+' value '+op.origin);
      const name = model.opcodes[def[0]];
      histogram[name] = (histogram[name] || 0) + 1;
    }
    commands.push({id,lane,begin,end,function:match[5],operations:ops.length,histogram,
      values:ops.map(o=>o.origin),contract_bindings:ops.flatMap(o=>boundary.get(o.origin)||[]),
      cache_objects:[...new Set((plan.instruction_cache || []).slice(begin,end).filter(x=>x!==null))]});
  }
  if (!commands.length || commands.reduce((n,c)=>n+c.operations,0)!==instructions.length)
    fail('source and plan command coverage differs, or shared-body emission is unsupported');
  const size = Math.max(...commands.map(c=>c.id))+1;
  const globals = `\n#include <stdlib.h>\nstatic uint64_t rds_profile_evals,rds_profile_commands[${size}];\n`
    + `__attribute__((destructor)) static void rds_profile_report(void){const char*p=getenv("RDS_REGION_ACTIVITY");if(!p)return;FILE*f=fopen(p,"w");if(!f)abort();fprintf(f,"evals %llu\\n",(unsigned long long)rds_profile_evals);for(unsigned i=0;i<${size};++i)fprintf(f,"command %u %llu\\n",i,(unsigned long long)rds_profile_commands[i]);fclose(f);}\n`;
  code = code.replace('#include <stdio.h>', '#include <stdio.h>'+globals);
  let instrumented=0;
  code = code.replace(/(\nstatic [^\n]*?\b(?:bound|direct)_(\d+)\([^\n]*?\{)/g,
    (_,head,id)=>{instrumented++;return head+`++rds_profile_commands[${id}];`;});
  if (instrumented!==commands.length) fail('generated function coverage differs');
  let phases=0;
  code = code.replace(/(\nstatic int phase_0_0\([^\n]*?\{)/g,(_,head)=>{phases++;return head+'++rds_profile_evals;';});
  if (phases!==1) fail('missing single-worker evaluation entry');
  fs.writeFileSync(prefix+'.c',code);
  fs.writeFileSync(prefix+'.json',JSON.stringify({source,planFile,modelFile,commands},null,2)+'\n');
} else if (mode === 'summarize' && args.length === 2) {
  const [prefix,activityFile] = args, report=JSON.parse(fs.readFileSync(prefix+'.json'));
  let evals=0;const counts=new Map();
  for(const line of fs.readFileSync(activityFile,'utf8').trim().split('\n')){
    const fields=line.split(' ');
    if(fields[0]==='evals')evals=Number(fields[1]);
    else if(fields[0]==='command')counts.set(Number(fields[1]),Number(fields[2]));
  }
  if(!evals)fail('no evaluation samples');
  let eager=0,executed=0;const histogram={};
  for(const command of report.commands){
    if(!counts.has(command.id))fail('missing command '+command.id);
    command.calls=counts.get(command.id);command.frequency=command.calls/evals;
    eager+=command.operations;executed+=command.operations*command.frequency;
    for(const [name,n]of Object.entries(command.histogram)){
      const h=histogram[name] ||= {eager:0,executed:0};h.eager+=n;h.executed+=n*command.frequency;
    }
  }
  const result={evals,eager_operations:eager,executed_operations_per_eval:executed,skipped_fraction:1-executed/eager,histogram,
    commands:report.commands.sort((a,b)=>(b.operations*(1-b.frequency))-(a.operations*(1-a.frequency)))};
  fs.writeFileSync(prefix+'.activity.json',JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify({evals,eager,executed,skipped_fraction:result.skipped_fraction},null,2));
} else fail('usage: node profile-regions.cjs emit source.c plan.json model.json prefix | summarize prefix activity.txt');
