/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Usually after a few invocations of this program, one of the calls will TIMEOUT. */

// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node chown.js

var fs = require('fs');

var uid = process.getuid();
var gid = process.getgid();

for (var i = 0; i < 1000; i++) {
  fs.chown('/tmp/raw.dat', uid, gid, (err) => {
    if (err) {
      console.log('Error:');
      console.log(err);
    }
  });
}
