#!/usr/bin/env node

var len = 31; // 31: 25 seconds
var str = '';
for (var i = 0; i < len; i++)
	str += 'a';
str += '!'; // mismatch

try {
	if (str.match(/(a+)+$/)) {
		console.log('Match');
	}
	else {
		console.log('Mismatch');
	}
} catch (e) {
	console.log('Caught exception:');
	console.log(e);
}
