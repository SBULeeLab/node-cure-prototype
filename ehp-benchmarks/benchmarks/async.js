const f = function(x){
  if (x == 9999999){
    return;
  }
  process.nextTick(() => f(x+1));
}
f(0);
