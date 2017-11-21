const f = function(x){
  if (x == 21){
    return;
  }

  process.nextTick(() => f(x+1));
  process.nextTick(() => f(x+1));
}
f(0);
