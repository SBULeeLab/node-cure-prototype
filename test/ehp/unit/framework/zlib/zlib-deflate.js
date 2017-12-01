// This should work.
// NODECURE_NODE_TIMEOUT_MS=999999 NODECURE_THREADPOOL_TIMEOUT_MS=999999 ../../../../../node zlib-deflate.js

var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');
var defBuf = zlib.deflateSync(buf);

console.log(`JS: Deflating`);
zlib.deflate(buf, (err, newDefBuf) => {
	console.log(`JS: Deflate complete, err ${err}`);
	if (defBuf.equals(newDefBuf))
		console.log(`JS: Success: async defBuf equals sync defBuf`);
	else
		console.log(`JS: Error, async defBuf != sync defBuf`);
});
