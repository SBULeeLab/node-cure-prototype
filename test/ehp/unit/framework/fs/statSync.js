// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node statSync.js

var fs = require('fs');

process.nextTick(() => {
  try {
    for (var i = 0; i < 10000; i++) {
      var stats = fs.statSync('/tmp/raw.dat');
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
