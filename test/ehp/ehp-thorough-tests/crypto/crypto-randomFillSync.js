// Asynchronous
const crypto = require('crypto');

const buf = Buffer.alloc(10*1024*1024);

console.log(`JS: calling randomFillSync`);
try {
	var res = crypto.randomFillSync(buf);
  console.log(`JS: randomFillSync completed`);
}
catch (e) {
  console.log(`JS: randomFillSync threw: ${e}`);
}

