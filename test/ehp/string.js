var len = 8 * 1024*1024; // MB

var a1 = '';
var a2 = '';
for (var i = 0; i < len; i++) {
	a1 += 'a';
}

console.log('allocated a1');

a2 = a1 + 'b';

console.log('allocated a2');

var before = process.hrtime();

a1 < a2;
a2 < a1;

if (a1 < a2) {
	console.log ('a1 < a2');
}
console.log(decimal(process.hrtime(before)));

function decimal(t) {
	var sec = '' + t[0];
	var nsec = '' + t[1];
	return sec + '.' + nsec.padStart(9, '0');
}
