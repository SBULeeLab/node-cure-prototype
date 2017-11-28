try {
	var len = 5 * 1024*1024; // MB

	var arr = [];
	for (var i = 0; i < len; i++) {
		var obj = { num : Math.random() };
		arr.push(obj);
	}

	var before = process.hrtime();
	arr.sort((a,b) => { return a.num < b.num });
	console.log(decimal(process.hrtime(before)));

	function decimal(t) {
		var sec = '' + t[0];
		var nsec = '' + t[1];
		return sec + '.' + nsec.padStart(9, '0');
	}
} catch (e) {
	console.log('Threw:');
	console.log(e);
}
