/**
 * redos in timer, then redos again in the exception handler.
 */

setTimeout(() => {
	console.log('JS: Entering timeout');
	var len = 30; // 31: 25 seconds
	var str = '';
	for (var i = 0; i < len; i++)
		str += 'a';
	str += '!'; // mismatch
	var re = /(a+)+$/;

	var ret = '';
	try {
		redos(str, re);
	} catch (e) {
		console.log('JS: Caught exception:');
		console.log(e);

		console.log('Another redos from exception handler -- should be an uncaught exception');
		redos(str, re);

		ret = e;
	}
}, 1);

function redos (str, re) {
		console.log('JS: redos: Performing match with str len ' + str.length);
	if (str.match(re))
		console.log('JS: Match');
	else 
		console.log('JS: Mismatch');
}
