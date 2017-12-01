/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// for i in `seq 1 100`; do NODECURE_NODE_TIMEOUT_MS=0 ../../../../../node fstatSync.js 2>&1 | grep 'ETIMEDOUT'; done

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

setTimeout(() => {
  for (var i = 0; i < 1000; i++) {
    try {
      var stats = fs.fstatSync(fd);
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
