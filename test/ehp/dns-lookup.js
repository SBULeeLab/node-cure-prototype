var dns = require('dns');
 
console.log('JS: dns lookup');
dns.lookup('www.google.com', (err, addr, fam) => {
	console.log(`JS: dns lookup complete: err ${err} addr ${addr} fam ${fam}`);
});
