/* This is hard to get to timeout, so try a bunch and with "immediate" timeout configured.
 * Sometimes segfaults with timeout 0, I wonder why. */

// NODECURE_NODE_TIMEOUT_MS=1 ../../../../../node accessSync.js

var fs = require('fs');

setTimeout(() => {
  try {
    for (var i = 0; i < 10000; i++) {
      try {
        fs.accessSync('/tmp/raw.dat');
      }
      catch (e) {
        console.log('accessSync threw:');
        console.log(e);
      }
    }
  }
  catch (e) {
    console.log('loop threw');
    console.log(e);
  }
});
