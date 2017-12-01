// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node readdir.js
// This is implemented in libuv as a call to scandir, which should time out.

var fs = require('fs');

fs.readdir('/tmp', (err, files) => {
	var nfiles = err ? -1 : files.length;
  console.log('err:');
  console.log(err);
  console.log('nfiles ' + nfiles);
});
