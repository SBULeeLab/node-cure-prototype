#!/usr/bin/env node

var fs = require('fs');

var fd = fs.openSync('/dev/random', fs.constants.O_RDONLY);
console.log(`fd ${fd}`);

var arr = new Array(4096);
var buf = Buffer.from(arr);
while(1) {
	var bs = fs.readSync(fd, buf, 0, 4096, 0);
	console.log(`Read ${bs} bytes`);
}
