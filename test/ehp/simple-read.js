var fs = require('fs');

fs.readFile('/tmp/raw.dat', (err, dat) => {
	var nbytes = err ? -1 : dat.length;
	console.log(`JS: err ${err} nbytes ${nbytes}`);
});
