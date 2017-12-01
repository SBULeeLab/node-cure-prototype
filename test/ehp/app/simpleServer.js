// Launch Node.cure: NODECURE_THREADPOOL_TIMEOUT_MS=1000 NODECURE_NODE_TIMEOUT_MS=100 NODECURE_ASYNC_HOOKS=1 NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy NODECURE_SILENT=1 ../../../node simpleServer.js 3000
// Launch baseline: node simpleServer.js 3001
// Run test: Invoke your favorite driver.

const _URL  = require('url'),
      _HTTP = require('http'),
      _FS   = require('fs');

var PORT = 3000;
if (process.argv.length > 2) {
  PORT = process.argv[2];
}
console.log(process.argv);
console.log(PORT);

var THROUGHPUT_REPORT_MS = 100;
var THROUGHPUT_REPORT_NS = THROUGHPUT_REPORT_MS * 1000000;

var nRequests = 0;
var timeSinceLastReport = [0,0]; // Time since last reported.
var nReported = 0;
function reportThroughput() {
  var since = process.hrtime(timeSinceLastReport);
  if (0 < since[0] || THROUGHPUT_REPORT_NS < since[1]) {
    var instantThroughput = nRequests / (since[0] + since[1]/1000000000);
    var sinceStartInSec = nReported * THROUGHPUT_REPORT_MS / 1000;
    console.error(`${sinceStartInSec}: instantaneousThroughput ${instantThroughput}`);
    console.log(`${sinceStartInSec} ${instantThroughput}`);
    nReported++;
    nRequests = 0;
    timeSinceLastReport = process.hrtime();
  }
}

function recordCompletedRequest() {
  if (nRequests === 0)
    timeSinceLastReport = process.hrtime(); // Start now, otherwise we overestimate the start-up cost

  if (3 < process.hrtime(timeSinceLastReport)[0]) { // Long gap, reset for ease of reporting data
    nRequests = 0;
    timeSinceLastReport = process.hrtime();
    nReported = 0;
    console.log('GAP\n\n\n\n');
    console.error('GAP\n\n\n\n');
  }

  nRequests++;
  reportThroughput();
}

const cb = (req, resp) => {  
  try {
    // Try-catch body is not optimized in V8, so move handler to a function.
    handleRequest(req, resp);
  }
  catch (e) {
    if (e.name === 'TimeoutError')
      resp.end('Request timed out'); // Could also use the socket timeout, since Event Loop can't be poisoned now and the timer CB would be triggered eventually.
    else
      throw e;
  }
}
const srv = _HTTP.createServer(cb).listen(PORT);

const handleRequest = (req, resp) => {
  let url = _URL.parse(req.url, true);
  let f = url.query.fileToRead;

  if (!f.match(/(\/.+)+$/)) // ReDoS
    resp.end('Invalid file');
  else {
    for (var i = 0; i < 1000; i++); // Do some processing.

    _FS.readFile(f, (err, d) => { // ReadDoS
        resp.end('The file you requested:' + d);
        recordCompletedRequest();
    });
  }
}
