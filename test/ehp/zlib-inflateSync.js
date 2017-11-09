var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');

var defBuf = zlib.deflateSync(buf);

console.log(`JS: Inflating`);
var infBuf = zlib.inflateSync(defBuf);
console.log(`JS: Inflate complete`);
if (buf.equals(infBuf))
	console.log(`JS: Success: buf equals inflate(deflate(buf))`);
else
	console.log(`JS: Error, buf != inflate(deflate(buf))`);
