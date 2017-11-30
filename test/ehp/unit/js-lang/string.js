/* NODECURE_NODE_TIMEOUT_MS=4000 ../../../../node string.js */

var len = 30 * 1024*1024; // MB

process.nextTick(()=> {
  try {
    var s1 = makeString(len);
    var s2 = s1 + 'b';

    console.log('Compare 1');
    s1 < s2;
    console.log('Compare 2');
    s2 < s1;
    console.log('Compare finished');
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});

function makeString(len) {
  var s = '';
  for (var i = 0; i < len; i++) {
    s += 'a';
  }

  return s;
}
