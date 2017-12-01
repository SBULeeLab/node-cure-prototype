process.on('uncaughtException', (err) => {
  fs.writeSync(1, `ERROR: Caught exception: ${err}\n`);
});

for (var i = 0; i < 100; i++) {
	setTimeout(() => {
		console.log('timer hello');
	}, 1);
}
