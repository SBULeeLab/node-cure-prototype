// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node symlink.js

var fs = require('fs');

var existingPath = '/tmp/raw.dat';
var newPath = '/tmp/raw.dat.LINK';

/* ENOENT */
try {
  fs.unlinkSync(newPath);
}
catch (e) {}

fs.symlink(existingPath, newPath, (err) => {
	console.log('Error:');
  console.log(err);
});
