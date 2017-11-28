// Asynchronous
const crypto = require('crypto');

const buf = Buffer.alloc(10*1024*1024);

console.log(`JS: calling randomFill`);
crypto.randomFill(buf, (err, buf) => {
  console.log(`JS: randomFill completed, err ${err}`); 
});

