/* Needs a link named /tmp/link
 * $ ln -s /tmp/pipe /tmp/link */

// Times out on lstat or readlink

// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node realpath.js

var fs = require('fs');

fs.realpath('/tmp/../tmp/../tmp/../tmp/../tmp/../tmp/link', (err, resolvedPath) => {
  console.log(`resolvedPath: ${resolvedPath}`);
  console.log('err:');
  console.log(err);
});
