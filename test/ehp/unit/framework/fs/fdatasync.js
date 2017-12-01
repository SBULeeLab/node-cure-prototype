// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node fdatasync.js

var fs = require('fs');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

var fd = fs.openSync('/tmp/raw.dat', 'w+');
fs.writeSync(fd, buf, 0, bs, 0);

fs.fdatasync(fd, (err) => {
  console.log('err:');
  console.log(err);
});
