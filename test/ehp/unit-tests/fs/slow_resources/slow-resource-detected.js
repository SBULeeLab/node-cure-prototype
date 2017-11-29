/* Test the Slow Resource Policy in the FS.
 * The first should finish slowly and the remainder should finish quickly.
 * cf. the uv log file. */

const fs = require('fs');

var slowFile = '/dev/random';

fs.readFile(slowFile, (err, buf) => {
  console.log('First finished');
  console.log(err);

  fs.readFile(slowFile, (err, buf) => {
    console.log('Second finished');
    console.log(err);

    fs.readFile(slowFile, (err, buf) => {
      console.log('Third finished');
      console.log(err);
    });
  });
});
