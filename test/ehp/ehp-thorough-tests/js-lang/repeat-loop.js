#!/usr/bin/env node

function looper () {
	var n = 0;
	var xs = 0;
	var ys = 0;
	while (1) {
		var now = process.hrtime();
		while (process.hrtime(now)[0] < 1);
		if (n) {
			xs++;
		}
		else {
			ys++;
		}
		n++;
		n = n % 2;
	}
}

while(1) {
	try {
		console.log('Calling looper');
		looper();
		console.log('Looper returned');
	} catch (e) {
		console.log("Caught e:");
		console.log(e);
	}
}
