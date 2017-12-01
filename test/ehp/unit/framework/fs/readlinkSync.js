/* Needs a link named /tmp/link
 * $ ln -s /tmp/pipe /tmp/link */

// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node readlinkSync.js

var fs = require('fs');

setTimeout(() => {
  for (var i = 0; i < 10000; i++) {
    try {
      var linkString = fs.readlinkSync('/tmp/link');
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
