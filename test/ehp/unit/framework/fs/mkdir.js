// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node mkdir.js

var fs = require('fs');

var dir = '/tmp/tmpdir_unique';

/* ENOENT */
try {
  fs.rmdirSync(dir);
}
catch (e) {}

fs.mkdir(dir, (err) => {
	console.log('Err:');
  console.log(err);
});
