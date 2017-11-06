const crypto = require('crypto');

console.log('JS: Calling pbkdf2');
crypto.pbkdf2('secret', 'salt', 100000, 64, 'sha512', (err, derivedKey) => {
  if (err) throw err;
  console.log(`JS: ${derivedKey.toString('hex')}`);  // '3745e48...08d59ae'
});

