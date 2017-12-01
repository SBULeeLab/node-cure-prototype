// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node dns-lookup.js

var dns = require('dns');
 
console.log('JS: dns lookup (getaddrinfo)');
dns.lookup('www.google.com', (err, addr, fam) => {
	console.log(`JS: dns lookup complete: err ${err} addr ${addr} fam ${fam}`);
  console.log('err:');
  console.log(err);
});
