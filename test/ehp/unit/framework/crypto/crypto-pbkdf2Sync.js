// Taken from https://nodejs.org/api/crypto.html#crypto_crypto_pbkdf2_password_salt_iterations_keylen_digest_callback
// Should time out.
// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node crypto-pbkdf2Sync.js

const crypto = require('crypto');

process.nextTick(() => {
  console.log(`JS: Calling pbkdf2Sync`);
  try {
    var derivedKey = crypto.pbkdf2Sync('secret', 'salt', 100000, 64, 'sha512');
    console.log(`JS: pbkdf2 completed: derivedKey ${keyStr}`);
  } catch(e) {
    console.log(`JS: pbkdf2Sync threw: err ${e}`);
  }
});
