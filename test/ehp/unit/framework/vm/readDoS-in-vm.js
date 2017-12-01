/*
 * Demonstrate that ReadDoS in a VM defeats the VM's timeout mechanism.
 * The behavior is that the readSync is not interrupted until the syscall returns.
 * Conclusion: Isolate::TerminateExecution cannot abort a blocked syscall.
 */

// This DOES NOT time out.
//   node readDoS-in-vm.js

// This DOES time out (yay!).
//  NODECURE_THREADPOOL_TIMEOUT_MS=100 NODECURE_NODE_TIMEOUT_MS=5000 ../../../../../node readDoS-in-vm.js

var vm = require('vm');
var fs = require('fs');

var ctxt = { require: require, Buffer: Buffer };
vm.createContext(ctxt);

var code = `
  var fs = require('fs');

  var fd = fs.openSync('/dev/random', fs.constants.O_RDONLY);
  console.log('fd ' + fd);

  var arr = new Array(4096);
  var buf = Buffer.from(arr);
  while(1) {
    var bs = fs.readSync(fd, buf, 0, 4096, 0);
    console.log('Read ' + bs + ' bytes');
  }
`;

var start = process.hrtime();
try {
	console.log('Running code in a context');
	console.log(code);
	var ret = vm.runInContext(code, ctxt, { timeout: 1000 });
	console.log('vm returned: ' + ret);
} catch (e) {
	console.log('vm threw after ' + process.hrtime(start));
	console.log(e);
}
