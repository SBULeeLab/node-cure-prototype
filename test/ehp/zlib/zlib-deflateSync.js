var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');
var defBuf = zlib.deflateSync(buf);

console.log(`JS: Deflating`);
var defBuf2 = zlib.deflateSync(buf);
console.log(`JS: Deflate complete`);
if (defBuf.equals(defBuf2))
	console.log(`JS: Success: the defBufs match`);
else
	console.log(`JS: Error, the defBufs mismatch`);
