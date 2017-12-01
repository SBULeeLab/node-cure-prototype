// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node utimesSync.js

var fs = require('fs');

process.nextTick(() => {
  try {
    for (var i = 0; i < 100000; i++) {
      fs.utimesSync('/tmp/raw.dat', '123', '123');
    }
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
