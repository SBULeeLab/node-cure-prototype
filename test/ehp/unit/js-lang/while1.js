setTimeout(() => {
	console.log('Starting while-1 loop');
	try {
		while(1);
	}
	catch (e) {
		console.log('Threw:');
    console.log(e);
	}
}, 1);
