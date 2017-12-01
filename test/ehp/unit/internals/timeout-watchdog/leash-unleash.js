/* The TimeoutWatchdog should propagate an appropriate timeout to fs.readFileSync in the sync2async approach.
 * If it fails to leash/unleash properly, an infinite loop of cheap reads can "cheat" and have their timer reset.
 *
 * Test with lazy and precise TimeoutWatchdog
 *
 * NODECURE_TIMEOUT_WATCHDOG_TYPE=precise ../../../../node fs-leash-test.js
 * NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy    ../../../../node fs-leash-test.js
 */

var fs = require('fs');

setTimeout(() => {
	console.log('JS: Beginning infinite loop of cheap FS reads');
	var i = 0;
	while (1) {
		fs.readFileSync(process.argv[1]); /* I read myself, therefore I am. */
		console.log(`JS: read ${i} finished`);
	}
}, 1);
