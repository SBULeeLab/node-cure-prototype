// This should timeout. zlib Tasks are partitioned so a very small threshold is needed.
// NODECURE_NODE_TIMEOUT_MS=999999 NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node zlib-inflate-timeout.js

var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');

var defBuf = zlib.deflateSync(buf);

console.log(`JS: Inflating`);
zlib.inflate(defBuf, (err, infBuf) => {
  console.log('err:');
  console.log(err);
});
