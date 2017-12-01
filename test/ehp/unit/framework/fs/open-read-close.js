// NODECURE_THREADPOOL_TIMEOUT_MS=100 ../../../../../node open-read-close.js

var fs = require('fs');

fs.open('/tmp/pipe', fs.constants.O_RDWR, (err, fd) => {
	if (err) {
		console.log(`JS: open err ${err}`);
		return;
	}

	var buf = Buffer.alloc(1024);

	fs.read(fd, buf, 0, 1024, null, (err, bsRead, buf) => {
		console.log(`JS: read err ${err} bsRead ${bsRead}`);

		fs.close(fd, (err) => {
			console.log(`JS: close err ${err}`);
		});

	});
});
