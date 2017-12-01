// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node ftruncateSync.js

var fs = require('fs');

var bigMB = 100 * 1024 * 1024;
var fd = fs.openSync('/tmp/raw.dat', 'a');

setTimeout(() => {
	try {
    for (var i = 0; i < 1000; i++) {
      fs.ftruncateSync(fd, bigMB);
    }
  }
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
