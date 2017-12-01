// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node link.js

var fs = require('fs');

var existingPath = '/tmp/raw.dat';
var newPath = '/tmp/raw.dat.LINK';

/* ENOENT */
try {
  fs.unlinkSync(newPath);
}
catch (e) {}

fs.link(existingPath, newPath, (err) => {
	console.log('Error:');
  console.log(err);
});
