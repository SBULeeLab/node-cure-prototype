/* Needs a link named /tmp/link
 * $ ln -s /tmp/pipe /tmp/link */

// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node readlink.js

var fs = require('fs');

fs.readlink('/tmp/link', (err, linkString) => {
  console.log('err:');
  console.log(err);
});
