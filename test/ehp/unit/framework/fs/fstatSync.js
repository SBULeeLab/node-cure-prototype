// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node fstatSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

process.nextTick(() => {
  try {
    for (var i = 0; i < 100000000; i++) {
      var stats = fs.fstatSync(fd);
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
