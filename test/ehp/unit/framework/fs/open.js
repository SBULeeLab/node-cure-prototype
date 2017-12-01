/* Needs a FIFO named '/tmp/pipe'.
 * $ mkfifo /tmp/pipe */

var fs = require('fs');

fs.openSync('/tmp/pipe', 'r', (err, dat) => {
	var bs = err ? -1 : dat.length;
	console.log(`JS: err ${err} bs ${bs}`);
});
