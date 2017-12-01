/* Needs a FIFO named '/tmp/pipe'.
 * $ mkfifo /tmp/pipe */

// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node openSync.js

var fs = require('fs');

setTimeout(() => {
	try {
		console.log('JS: hung readFileSync');
		var dat = fs.openSync('/tmp/pipe', 'r');
	}
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
