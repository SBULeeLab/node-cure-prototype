var fs = require('fs');

setTimeout(() => {
	try {
		console.log('JS: hung readFileSync');
		var dat = fs.readFileSync('/tmp/pipe');
	}
	catch (err) {
		console.log(`JS: caught err ${err}`); 
	}
}, 1);
