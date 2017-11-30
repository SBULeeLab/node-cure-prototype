/* NODECURE_NODE_TIMEOUT_MS=2000 ../../../../node sort.js  */

var len = 10 * 1024*1024; // MB

/* With language-defined < */
setTimeout(() => {
  try {
    arr = makeArray(len);

    console.log('lang <: sort');
    arr.sort();
    console.log('lang <: Completed');

  } catch (e) {
    console.log('lang <: Threw:');
    console.log(e);
  }
}, 1);

/* With user-defined < */
process.nextTick(() => {
  try {
    arr = makeArray(len);

    console.log('user <: sort');
    arr.sort((a,b) => { return a.num < b.num });
    console.log('user <: Completed');
  } catch (e) {
    console.log('user <: Threw:');
    console.log(e);
  }
});

process.nextTick(() => {
  try {
    arr = makeArray(10);

    console.log('short user <: sort');
    arr.sort((a,b) => { return a.num < b.num });
    console.log('short user <: Completed');
  } catch (e) {
    console.log('short user <: Threw:');
    console.log(e);
  }
});

function makeArray(len) {
  var arr = [];
  for (var i = 0; i < len; i++) {
    var obj = { num : Math.random() };
    arr.push(obj);
  }
  return arr;
}
