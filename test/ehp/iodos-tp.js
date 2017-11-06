#!/usr/bin/env node

var fs = require('fs');

var i = 0;

console.log('JS: fs.open');
fs.open('/dev/random', fs.constants.O_RDONLY, (err, fd) => {
	console.log(`fd ${fd}`);
	var arr = new Array(4096);
	var buf = Buffer.from(arr);

	console.log('JS: Starting fs.read');
	doRead(fd, buf);
});

function doRead (fd, buf) {
	fs.read(fd, buf, 0, 4096, 0, (err, data) => {
		console.log(`JS: Read ${i} complete, err ${err}`);
		i++;
		setTimeout(()=>{
			doRead(fd, buf);
		}, 250);
	});
}
