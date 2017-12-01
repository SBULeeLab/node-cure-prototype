// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node appendFileSync.js
var fs = require('fs');

buf = Buffer.alloc(10 * 1024*1024); /* 10 MB */

setTimeout(() => {
	try {
		console.log('JS: big appendFileSync');
    fs.appendFileSync('/tmp/appendFileTest', buf);
		console.log('JS: finished');
	}
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
