var fs = require('fs');

/* Queue a ton of requests. Some should learn that it is slow and return promptly. */
for (var i = 0; i < 100; i++) {
	fs.readFile('/tmp/pipe', (err, dat) => {
		var bs = err ? -1 : dat.length;
		console.log(`JS: err ${err} bs ${bs}`);
	});
}
