// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node renameSync.js

var fs = require('fs');

var src = '/tmp/rename-file-src';
var dst = '/tmp/rename-file-dst';

var fd = fs.openSync(src, 'w');
fs.closeSync(fd);

process.nextTick(() => {
  try {
    for (var i = 0; i < 10000; i++) {
      fs.renameSync(src, dst);
      fs.renameSync(dst, src);
    }
  }
  catch (e) {
    console.log('Error:');
    console.log(e);
  }
});
