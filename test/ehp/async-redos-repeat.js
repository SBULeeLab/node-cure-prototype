/** 
 * Run forever, catching all timeout exceptions that are thrown and then repeatedly timing out again.
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
	while (1) {
		console.log('JS: Entering pathological infinite loop');
		try {
			redos(str, re);
		} catch (e) {
			console.log('JS: Caught exception:');
			console.log(e);
			ret = e;
		}
	}
}, 1);

function redos (str, re) {
		console.log('JS: redos: Performing match with str len ' + str.length);
	if (str.match(re))
		console.log('JS: Match');
	else 
		console.log('JS: Mismatch');
}
