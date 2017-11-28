let max_depth = 19;
if (process.argv.length > 2){
  max_depth = parseInt(process.argv[2],10);
}
const f = function(x){
  if (x == max_depth){
    return;
  }

  process.nextTick(() => f(x+1));
  process.nextTick(() => f(x+1));
}
f(0);
