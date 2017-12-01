// NODECURE_NODE_TIMEOUT_MS=5 ../../../../../node writeSync.js

var fs = require('fs');

var fd = fs.openSync('/tmp/raw.dat', 'w+');

var bs = 10*1024*1024;
var buf = Buffer.alloc(bs);

setTimeout(() => {
  for (var i = 0; i < 10000; i++) {
    try {
      fs.writeSync(fd, buf, 0, bs, 0);
    }
    catch (e) {
      console.log('Error:');
      console.log(e);
    }
  }
});
