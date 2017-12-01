// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node mkdirSync.js

var fs = require('fs');

var dir = '/tmp/tmpdir_unique';

/* ENOENT */
try {
  fs.rmdirSync(dir);
}
catch (e) {}

setTimeout(() => {
  try {
    for (var i = 0; i < 1000000; i++) {
      fs.mkdirSync(dir);
      fs.rmdirSync(dir);
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
