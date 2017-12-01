/* Needs a FIFO named '/tmp/pipe'.
 * $ mkfifo /tmp/pipe */

// NODECURE_THREADPOOL_TIMEOUT_MS=100 ../../../../../node open.js

var fs = require('fs');

fs.open('/tmp/pipe', 'r', (err, dat) => {
	var bs = err ? -1 : dat.length;
	console.log(`JS: err ${err} bs ${bs}`);
});
