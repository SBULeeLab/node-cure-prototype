// This should work.
// NODECURE_NODE_TIMEOUT_MS=999999 NODECURE_THREADPOOL_TIMEOUT_MS=999999 ../../../../../node zlib-inflate.js

var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');

var defBuf = zlib.deflateSync(buf);

console.log(`JS: Inflating`);
zlib.inflate(defBuf, (err, infBuf) => {
	console.log(`JS: Inflate complete, err ${err}`);
	if (buf.equals(infBuf))
		console.log(`JS: Success: buf equals inflate(deflate(buf))`);
	else
		console.log(`JS: Error, buf != inflate(deflate(buf))`);
});
