// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node readdirSync.js
// This is implemented in libuv as a call to scandir, which should time out.

var fs = require('fs');

setTimeout(() => {
  try {
    for (var i = 0; i < 100; i++)
      var files = fs.readdirSync('/tmp');
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
