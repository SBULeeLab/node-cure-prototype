var fs = require('fs');

fs.readFile('/tmp/pipe', (err, dat) => {
	var bs = err ? -1 : dat.length;
	console.log(`JS: err ${err} bs ${bs}`);
});
