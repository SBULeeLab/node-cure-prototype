// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node futimesSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

process.nextTick(() => {
  try {
    for (var i = 0; i < 100000; i++) {
      fs.futimesSync(fd, '123', '123');
    }
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
