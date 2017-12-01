/* Needs a FIFO named '/tmp/pipe'.
 * $ mkfifo /tmp/pipe */

var fs = require('fs');

setTimeout(() => {
	try {
		console.log('JS: hung readFileSync');
		var dat = fs.openSync('/tmp/pipe', 'r');
	}
	catch (err) {
    console.log('Threw:');
    console.log(err);
	}
}, 1);
