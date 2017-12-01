/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node chmod.js

var fs = require('fs');

for (var i = 0; i < 1000; i++) {
  fs.chmod('/tmp/raw.dat', 0777, (err) => {
    if (err) {
      console.log('Error:');
      console.log(err);
    }
  });
}
