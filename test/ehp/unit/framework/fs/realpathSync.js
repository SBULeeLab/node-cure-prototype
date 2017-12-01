/* Needs a link named /tmp/link
 * $ ln -s /tmp/pipe /tmp/link */

// Times out on lstat or readlink

// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node realpathSync.js

var fs = require('fs');

setTimeout(() => {
  for (var i = 0; i < 10000; i++) {
    try {
      var resolvedPath = fs.realpathSync('/tmp/../tmp/../tmp/../tmp/../tmp/../tmp/link');
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
