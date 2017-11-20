var fs = require('fs');

console.log(`JS: Launching async read`);
fs.readFile('/tmp/raw.dat', (err, data) => {
	console.log(`JS: Read complete (err ${err})`);
});
