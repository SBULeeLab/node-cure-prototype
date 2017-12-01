// Asynchronous
// Should time out.
// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node crypto-randomFillSync.js
const crypto = require('crypto');

const buf = Buffer.alloc(10*1024*1024);

process.nextTick(() => {
  console.log(`JS: calling randomFillSync`);
  try {
    var res = crypto.randomFillSync(buf);
    console.log(`JS: randomFillSync completed`);
  }
  catch (e) {
    console.log(`JS: randomFillSync threw: ${e}`);
  }
});
