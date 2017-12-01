// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node writeFile.js

var fs = require('fs');

var buf = Buffer.alloc(10*1024*1024);

fs.writeFile('/tmp/raw.dat', buf, (err) => {
  console.log('err:');
  console.log(err);
});
