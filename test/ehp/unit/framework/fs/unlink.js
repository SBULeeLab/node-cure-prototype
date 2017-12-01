// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node unlink.js

var fs = require('fs');

var existingPath = '/tmp/raw.dat';
var newPath = '/tmp/raw.dat.LINK';

/* ENOENT */
try {
  fs.linkSync(existingPath, newPath);
}
catch (e) {}

fs.unlink(newPath, (err) => {
	console.log('Error:');
  console.log(err);
});
