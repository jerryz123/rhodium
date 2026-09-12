// Adds a failing host effect after TLB preparation without repeating frontend elaboration.
'use strict';
const fs=require('fs'),path=require('path');
const directory=process.argv[2],model=JSON.parse(fs.readFileSync(path.join(directory,'tlb-2.json')));
const reset=model.objects[0][4][0];
model.objects.push([8,0,8,0,[reset,reset,reset,reset,reset],'failure-host']);
fs.writeFileSync(path.join(directory,'tlb-host.json'),JSON.stringify(model)+'\n');
