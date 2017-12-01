// Taken from https://nodejs.org/api/crypto.html#crypto_crypto_pbkdf2_password_salt_iterations_keylen_digest_callback
// Should time out.
// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node crypto-pbkdf2.js 

const crypto = require('crypto');

console.log(`JS: Calling pbkdf2`);
crypto.pbkdf2('secret', 'salt', 100000, 64, 'sha512', (err, derivedKey) => {
  var keyStr = err ? '' : derivedKey.toString('hex');
	console.log(`JS: pbkdf2 completed: err ${err} derivedKey ${keyStr}`);
});
