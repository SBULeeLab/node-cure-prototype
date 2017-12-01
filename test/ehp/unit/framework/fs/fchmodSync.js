/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node fchmodSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'a');

setTimeout(() => {
  for (var i = 0; i < 1000; i++) {
    try {
      fs.fchmodSync(fd, 0777);
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
