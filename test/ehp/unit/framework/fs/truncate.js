// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node truncate.js
// This will usually time out with open, but sometimes with ftruncate.
// Apparently node converts truncate to open-ftruncate.

var fs = require('fs');

var bigMB = 100 * 1024 * 1024;

fs.truncate('/tmp/raw.dat', (err) => {
  if (err) {
    console.log('err:');
    console.log(err);
  }

  fs.truncate('/tmp/raw.dat', bigMB, (err) => {
    if (err) {
      console.log('err:');
      console.log(err);
    }

    try {
      fs.truncateSync('/tmp/raw.dat', bigMB);
    } catch (e){}
  });
});
