// Taken from https://nodejs.org/api/crypto.html#crypto_crypto_pbkdf2_password_salt_iterations_keylen_digest_callback

const crypto = require('crypto');

console.log(`JS: Calling pbkdf2Sync`);
try {
var derivedKey = crypto.pbkdf2Sync('secret', 'salt', 100000, 64, 'sha512');
	console.log(`JS: pbkdf2 completed: derivedKey ${keyStr}`);
} catch(e) {
	console.log(`JS: pbkdf2 threw: err ${e}`);
}
