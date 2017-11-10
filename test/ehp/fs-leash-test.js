var fs = require('fs');

setTimeout(() => {
	console.log('JS: Beginning infinite loop of cheap FS reads');
	var i = 0;
	for (var i = 0; i < 100; i++) {
		fs.readFileSync('/tmp/raw.dat');
		console.log(`JS: read ${i} finished`);
	}
}, 1);
