// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node fsyncSync.js

var fs = require('fs');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

var fd = fs.openSync('/tmp/raw.dat', 'w+');
fs.writeSync(fd, buf, 0, bs, 0);

process.nextTick(() => {
  try {
    for (var i = 0; i < 10000; i++) {
      fs.fsyncSync(fd);
    }
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
