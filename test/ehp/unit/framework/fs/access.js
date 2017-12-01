/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Sometimes segfaults with timeout 0, I wonder why. */

// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node access.js

var fs = require('fs');

for (var i = 0; i < 100; i++) {
  fs.access('/tmp/raw.dat', (err) => {
    if (err) {
      console.log('Error:');
      console.log(err);
    }
  });
}
