// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node truncateSync.js

var fs = require('fs');

var bigMB = 100 * 1024 * 1024;

setTimeout(() => {
	try {
    for (var i = 0; i < 1000; i++) {
      fs.truncateSync('/tmp/raw.dat', bigMB);
    }
  }
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
