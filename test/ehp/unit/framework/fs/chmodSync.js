/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node chmodSync.js

var fs = require('fs');

setTimeout(() => {
  for (var i = 0; i < 10000; i++) {
    try {
      fs.chmodSync('/tmp/raw.dat', 0777);
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
