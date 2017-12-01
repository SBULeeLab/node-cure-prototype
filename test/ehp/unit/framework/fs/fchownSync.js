/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node chownSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'a');
var uid = process.getuid();
var gid = process.getgid();

setTimeout(() => {
	try {
    for (var i = 0; i < 1000; i++) {
      fs.fchownSync(fd, uid, gid);
    }
  }
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
