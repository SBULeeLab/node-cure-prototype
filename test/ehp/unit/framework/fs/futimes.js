// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node futimes.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'r');

fs.futimes(fd, '123', '123', (err) => {
  console.log('Err:');
  console.log(err);
});
