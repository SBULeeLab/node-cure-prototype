// This should timeout.
// NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy NODECURE_NODE_TIMEOUT_MS=1000 NODECURE_THREADPOOL_TIMEOUT_MS=999999 ../../../../../node zlib-deflateSync-timeout.js

// TODO precise TimeoutWatchdog fails:
// (00:06:17) jamie@jamie-Lenovo-K450e ~/Desktop/ehp-attacks/node-cure/test/ehp/unit/framework/zlib $ NODECURE_NODE_TIMEOUT_MS=1000 NODECURE_THREADPOOL_TIMEOUT_MS=999999 ../../../../../node zlib-deflateSync-timeout.js
// CB begins
// /home/jamie/Desktop/ehp-attacks/node-cure/out/Release/node[18437]: ../src/node_watchdog.cc:289:virtual void node::PreciseTimeoutWatchdog::AfterHook(long int): Assertion `!leashed_' failed.
//  1: node::Abort() [../../../../../node]
//  2: node::Assert(char const* const (*) [4]) [../../../../../node]
//  3: node::PreciseTimeoutWatchdog::AfterHook(long) [../../../../../node]
//  4: 0x1281cc2 [../../../../../node]
//  5: v8::internal::FunctionCallbackArguments::Call(void (*)(v8::FunctionCallbackInfo<v8::Value> const&)) [../../../../../node]
//  6: 0xb258f7 [../../../../../node]
//  7: v8::internal::Builtin_HandleApiCall(int, v8::internal::Object**, v8::internal::Isolate*) [../../../../../node]
//  8: 0x3f1a5fe8463d
// Aborted (core dumped)

var fs = require('fs');
var zlib = require('zlib');

var buf = fs.readFileSync('/tmp/raw-huge.dat');

process.nextTick(() => {
  console.log('CB begins');
  try {
    console.log(`JS: Deflating 1`);
    var defBuf = zlib.deflateSync(buf);
    console.log(`JS: Deflating 2`);
    var defBuf2 = zlib.deflateSync(buf);
    console.log(`JS: Deflate complete`);

    if (defBuf.equals(defBuf2))
      console.log(`JS: Success: the defBufs match`);
    else
      console.log(`JS: Error, the defBufs mismatch`);
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
