// NODECURE_THREADPOOL_TIMEOUT_MS=0 ../../../../../node ftruncate.js

var fs = require('fs');

var bigMB = 100 * 1024 * 1024;
var fd = fs.openSync('/tmp/raw.dat', 'a');

fs.ftruncate(fd, (err) => {
  if (err) {
    console.log('err:');
    console.log(err);
  }

  fs.ftruncate(fd, bigMB, (err) => {
    if (err) {
      console.log('err:');
      console.log(err);
    }

    try {
      fs.ftruncateSync(fd, bigMB);
    } catch (e){}
  });
});
