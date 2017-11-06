var dns = require('dns');
 
console.log('JS: dns lookupService (getnameinfo)');
dns.lookupService('128.173.237.147', 2200, (err, hostname, service) => {
	console.log(`JS: dns lookupService complete: err ${err} hostname ${hostname} service ${service}`);
});
