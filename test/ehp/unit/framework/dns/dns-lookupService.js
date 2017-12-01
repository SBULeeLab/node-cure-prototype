// NODECURE_THREADPOOL_TIMEOUT_MS=1 ../../../../../node dns-lookupService.js

// TODO This coredumps occasionally
// node: ../deps/uv/src/unix/getnameinfo.c:110: uv__getnameinfo_done: Assertion `req->retcode == -ETIMEDOUT' failed.
// Aborted (core dumped)

var dns = require('dns');
 
console.log('JS: dns lookupService (getnameinfo)');
dns.lookupService('128.173.237.147', 2200, (err, hostname, service) => {
	console.log(`JS: dns lookupService complete: err ${err} hostname ${hostname} service ${service}`);
  console.log('err:');
  console.log(err);
});
