setTimeout(()=>{
	var obj = { a: 1 };
	var len = 21;

	var n, took;

	for (var i = 0; i < len; i++) {
		obj = { obj1: obj, obj2: obj }; // Doubles in size each iter
	}

	console.log('len ' + len + ' : stringify');
	n = process.hrtime();
	var str = JSON.stringify(obj);
	took = process.hrtime(n);
	console.log('stringify took ' + took);

	n = process.hrtime();
	indexOf = str.indexOf('nomatch');
	took = process.hrtime(n);
	console.log('indexof took ' + took);

	console.log('len ' + len + ' : str.length ' + str.length +', parse');
	n = process.hrtime();
	var p = JSON.parse(str);
	took = process.hrtime(n);
	console.log('done, parse took ' + took);
}, 1);
