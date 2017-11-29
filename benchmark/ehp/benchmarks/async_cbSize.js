/* Measure amortization point of node.cure's TimeoutWatchdog. */

const buf = Buffer.alloc(1);

let nCBs = 100;
let nLoops = 100;

if (process.argv.length > 2)
  nCBs = parseInt(process.argv[2], 10);

if (process.argv.length > 3)
  nLoops = parseInt(process.argv[3], 10);

console.log(`nCBs ${nCBs} nLoops ${nLoops}`);

let i = 0;
var nextTickFunc = function(){
  for (let j = 0; j < nLoops; j++);
  i++;
  if (i < nCBs)
    process.nextTick(nextTickFunc);
}

process.nextTick(nextTickFunc);
