/* Needs a large-ish (100MB) file named '/tmp/raw.dat'.
 * $ dd if=/dev/zero of=/tmp/raw.dat bs=1 count=1 seek=100M */

// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node readFileSync.js

var fs = require('fs');

setTimeout(() => {
	try {
		console.log('JS: big readFileSync');
		var dat = fs.readFileSync('/tmp/raw.dat');
		console.log('JS: finished');
	}
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
