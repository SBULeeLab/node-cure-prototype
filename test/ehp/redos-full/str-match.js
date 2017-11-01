#!/usr/bin/env node

var len = 30; // 30: 12 seconds
var str = '';
for (var i = 0; i < len; i++)
	str += 'a';
str += '!'; // mismatch

try {
	console.log('Performing match with str len ' + str.length);
	if (str.match(/(a+)+$/))
		console.log('Match');
	else
		console.log('Mismatch');
} catch (e) {
	console.log('Caught exception:');
	console.log(e);
}
