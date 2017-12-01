// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node symlinkSync.js

var fs = require('fs');

var existingPath = '/tmp/raw.dat';
var newPath = '/tmp/raw.dat.LINK';

/* ENOENT */
try {
  fs.unlinkSync(newPath);
}
catch (e) {}

process.nextTick(() => {
  try {
    for (var i = 0; i < 1000000; i++) {
      fs.symlinkSync(existingPath, newPath);
      fs.unlinkSync(newPath);
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
