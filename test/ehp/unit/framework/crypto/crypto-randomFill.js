// Asynchronous
// Should time out.
// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node crypto-randomFill.js 
const crypto = require('crypto');

const buf = Buffer.alloc(10*1024*1024);

console.log(`JS: calling randomFill`);
crypto.randomFill(buf, (err, buf) => {
  console.log(`JS: randomFill completed, err ${err}`); 
});

