const buf = Buffer.alloc(1);
const crypto = require('crypto');


let max_depth = 20;
if (process.argv.length > 2){
  max_depth = parseInt(process.argv[2],10);
}


const f = function(x){
  if (x == max_depth){
    return;
  }
  crypto.randomFill(buf, 0, 0, () => f(x+1));
  crypto.randomFill(buf, 0, 0, () => f(x+1));
}
f(0);
