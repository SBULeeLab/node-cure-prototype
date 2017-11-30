/*  NODECURE_NODE_TIMEOUT_MS=100 ../../../../node ignore-some-timeouts.js */

var nTimeoutsToIgnore = 5;

var nTimeouts = 0;

/* Loop for 1 second. */
function looper () {
  var now = process.hrtime();
  while (process.hrtime(now)[0] < 1);
}

process.nextTick(() => {
  while(1) {
    try {
      console.log('Calling looper');
      looper();
      console.log('Looper returned');
    } catch (e) {
      console.log("Threw:");
      console.log(e);

      if (e.name === 'TimeoutError')
        nTimeouts++;

      if (nTimeouts === nTimeoutsToIgnore) {
        console.log(`I've ignored ${nTimeoutsToIgnore} timeouts already, giving up`);
        process.exit(0);
      }
    }
  }
});
