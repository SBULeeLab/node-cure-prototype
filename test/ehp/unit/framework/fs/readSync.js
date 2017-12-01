/* Needs a large-ish (100MB) file named '/tmp/raw.dat'.
 * $ dd if=/dev/zero of=/tmp/raw.dat bs=1 count=1 seek=100M */

// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node readSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

setTimeout(() => {
  try {
    for (var i = 0; i < 10000; i++) {
      fs.readSync(fd, buf, 0, bs, 0);
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
