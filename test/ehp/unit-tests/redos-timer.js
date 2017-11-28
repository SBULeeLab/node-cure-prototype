/* Node regexp match will run to completion by default.
 * With NodeCure it will throw an exception.
 */

setTimeout(() => {
	var len = 28; // 31: 25 seconds
	var str = '';
	for (var i = 0; i < len; i++)
		str += 'a';
	str += '!'; // mismatch

	var ret = '';
	try {
		console.log('Performing match with str len ' + str.length);
		if (str.match(/(a+)+$/)) {
			console.log('Match');
		}
		else {
			console.log('Mismatch');
		}
	} catch (e) {
		console.log('Caught exception:');
		console.log(e);
		ret = e;
	}

	ret; // If run from a VM, this is the return value.
}, 1);
