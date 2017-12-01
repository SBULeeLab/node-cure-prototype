// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node appendFile.js
var fs = require('fs');

buf = Buffer.alloc(10 * 1024*1024); /* 10 MB */

fs.appendFile('/tmp/appendFileTest', buf, (err) => {
  console.log('err:');
  console.log(err);
});
