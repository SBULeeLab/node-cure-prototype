// Asynchronous
const crypto = require('crypto');

console.log(`JS: calling randomBytes`);
crypto.randomBytes(10*1024*1024, (err, buf) => {
  var bufBytes = err ? '-1' : buf.length;
  console.log(`JS: err ${err} ${bufBytes} bytes of random data`);
});

