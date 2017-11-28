setTimeout(() => {
	console.log('Starting while-1 loop');
	try {
		while(1);
	}
	catch (e) {
		console.log(`threw: ${e}`);
	}
}, 1);
