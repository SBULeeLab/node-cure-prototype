var dns = require('dns');
 
console.log('JS: dns reverse');
dns.reverse('128.173.237.147', (err, hostnames) => {
	console.log(`JS: dns reverse complete: err ${err} hostnames ${hostnames}`);
});
