/* Needs a large-ish (100MB) file named '/tmp/raw.dat'.
 * $ dd if=/dev/zero of=/tmp/raw.dat bs=1 count=1 seek=100M */

// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node read.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

fs.read(fd, buf, 0, bs, 0, (err, bytesRead, buf) => {
  console.log('err:');
  console.log(err);
  console.log('bytesRead ' + bytesRead);
});
