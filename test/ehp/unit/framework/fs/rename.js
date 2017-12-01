// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node rename.js

var fs = require('fs');

var src = '/tmp/rename-file-src';
var dst = '/tmp/rename-file-dst';

var fd = fs.openSync(src, 'w');
fs.closeSync(fd);

fs.rename(src, dst, (err) => {
	console.log('Error:');
  console.log(err);
});
