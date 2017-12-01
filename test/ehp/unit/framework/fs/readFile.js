/* Needs a large-ish (100MB) file named '/tmp/raw.dat'.
 * $ dd if=/dev/zero of=/tmp/raw.dat bs=1 count=1 seek=100M */

// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node readFile.js

var fs = require('fs');

fs.readFile('/tmp/raw.dat', (err, dat) => {
	var bs = err ? -1 : dat.length;
  console.log('err:');
  console.log(err);
  console.log('bs ' + bs);
});
