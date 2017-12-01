// This should time out.
// for i in `seq 1 1000`; do NODECURE_NODE_TIMEOUT_MS=999999 NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node zlib-pipe-timeout.js ; done

var fs = require('fs');
var zlib = require('zlib');

var gzip = zlib.createGzip();
var inp = fs.createReadStream('/tmp/raw.dat');
var out = fs.createWriteStream('/tmp/raw.gz');

process.on('uncaughtException', (err) => {
  // This means an exception was thrown somewhere in the pipe logic, e.g. zipping or I/O.
  fs.writeSync(1, `Caught exception: ${err}\n`);
});
 
console.log('JS: Piping /tmp/raw.dat into /tmp/raw.gz');
inp.pipe(gzip).pipe(out);
console.log('JS: Done');
