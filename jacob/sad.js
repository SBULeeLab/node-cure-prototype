process.env.UV_THREADPOOL_SIZE = 6;
const fs = require('fs');

function bad() {
  fs.readFile('/dev/random', 'utf8', function(err, content) {
    console.log('finished opening /dev/random');
  });
}

function sad() {
  fs.readFile(__dirname + '/sad.js', 'utf8', function (err, content) {
    if (err) {
      console.log(err.message);
    } else {
      console.log('got sad.js!');
    }
  });
}

for (let i = 0; i < 5; i++) {
  bad();
}

sad();
