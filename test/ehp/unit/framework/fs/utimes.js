// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node utimes.js

var fs = require('fs');

fs.utimes('/tmp/raw.dat', '123', '123', (err) => {
  console.log('Err:');
  console.log(err);
});
