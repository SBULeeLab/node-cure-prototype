// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node rmdir.js

var fs = require('fs');

var dir = '/tmp/tmpdir_unique';

/* ENOENT */
try {
  fs.mkdirSync(dir);
}
catch (e) {}

fs.rmdir(dir, (err) => {
	console.log('Err:');
  console.log(err);
});
