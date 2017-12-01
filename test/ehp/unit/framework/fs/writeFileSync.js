// NODECURE_NODE_TIMEOUT_MS=1 ../../../../../node writeFileSync.js

var fs = require('fs');

var buf = Buffer.alloc(10*1024*1024);

try {
  for (var i = 0; i < 10; i++)
    fs.writeFileSync('/tmp/raw.dat', buf);
}
catch (e) {
  console.log('err:');
  console.log(e);
}
