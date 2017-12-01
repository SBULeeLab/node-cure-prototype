// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node write.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'w+');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

fs.write(fd, buf, 0, bs, 0, (err, bsWritten, buf) => {
  console.log('err:');
  console.log(err);
  console.log('bs ' + bsWritten);
});
