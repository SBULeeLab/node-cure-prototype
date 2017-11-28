var fs = require('fs');

setTimeout(() => {
	console.log('JS: Beginning infinite loop of cheap FS reads');
	var i = 0;
	while (1) {
		fs.readFileSync('/tmp/raw.dat');
		console.log(`JS: read ${i} finished`);
	}
}, 1);
