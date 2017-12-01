/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// for i in `seq 1 100`; do NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node statSync.js 2>&1 | grep 'ETIMEDOUT: connection timed out, stat'; done

var fs = require('fs');

setTimeout(() => {
  for (var i = 0; i < 10000; i++) {
    try {
      var stats = fs.statSync('/tmp/raw.dat');
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
